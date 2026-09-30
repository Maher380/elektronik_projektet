/**
 * @file a89301_config_test.cpp
 * @brief A89301 configuration app over I2C, run when A89301_CONFIG_MODE is defined in main.cpp.
 *
 * @note Remove this file (and the A89301_CONFIG_MODE define in main.cpp) once no longer needed.
 */

#include "test_app/test_app.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "driver/adc/esp32s3.h"
#include "driver/gpio/esp32s3.h"
#include "driver/i2c/esp32s3.h"
#include "driver/motor/a89301_programmer.h"
#include "driver/odometer/a3144.h"
#include "driver/serial/esp32s3.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app::test_app
{

void runA89301ConfigTest() noexcept
{
    // A89301 configuration app over I2C. See helpText for commands.
    // Wiring: SPD/SCL -> A5 (GPIO12), FG/SDA -> A4 (GPIO11), 4.7 kOhm pull-up SCL -> 3.3 V,
    // DIR -> D4 (GPIO7), BRAKE -> D2 (GPIO5), IOREF -> 3V3, A3144 wheel sensor -> D9 (GPIO18),
    // TMP36 on the motor can -> A0 (GPIO1). Start the ESP32 before powering VIN.
    // Safety: spd runs and sweeps stop above maxMotorTempC or when the chip drives a wheel that does not turn.
    namespace a89301 = driver::motor::a89301;

    constexpr std::uint8_t sdaPin{11U};       // A4 / GPIO11 -> FG/SDA
    constexpr std::uint8_t sclPin{12U};       // A5 / GPIO12 -> SPD/SCL
    constexpr std::uint8_t directionPin{7U};  // D4 / GPIO7 -> DIR
    constexpr std::uint8_t brakePin{5U};      // D2 / GPIO5 -> BRAKE
    constexpr std::uint8_t odometerPin{18U};  // D9 / GPIO18 <- A3144 wheel sensor
    constexpr std::uint8_t wheelPulsesPerRev{1U};
    constexpr std::uint8_t motorTempPin{1U};  // A0 / GPIO1 <- TMP36 taped to the motor can
    // Temporarily lowered (60 -> 45, 40 -> 32) because the TMP36 sits on two layers of electrical tape,
    // so it reads low and late. Raise again when the sensor has direct contact with the motor can.
    constexpr float maxMotorTempC{45.0F};     // Motor is stopped above this can temperature.
    constexpr float coolMotorTempC{32.0F};    // Sweep waits until the can is below this temperature.
    constexpr double stallMinSpeedHz{50.0};   // Chip estimate above which the wheel must turn.
    constexpr std::int64_t stallTimeoutUs{1'000'000}; // No wheel pulse for this long = stalled.
    constexpr std::uint32_t safetyPeriodMs{100U};
    constexpr std::uint32_t pollPeriodMs{10U};
    constexpr std::uint32_t monitorPeriodMs{100U};
    constexpr const char* helpText{
        "Commands:\n"
        "  p                probe the A89301 on I2C\n"
        "  d                dump EEPROM and working registers with decoded fields\n"
        "  r <reg>          read one register (decimal or 0x hex)\n"
        "  m                toggle live monitor (speed, wheel, currents, VBB, demand, state)\n"
        "  set <FIELD> <v>  change a field in the working register (temporary)\n"
        "  spd <0.0 - 1.0>  drive with I2C speed control (releases brake)\n"
        "  stop             speed demand 0 and brake on\n"
        "  f / b            direction pin forward / backward\n"
        "  save             show working registers that differ from EEPROM\n"
        "  save yes         program those differences into EEPROM (keep VIN on!)\n"
        "  sweep [speed] [seconds] [list]\n"
        "                   test PID_P/PID_I combinations, defaults 0.3, 5 s, list 1\n"
        "                   (list 2 = refined PID, list 3 = PID with MOTOR_INDUCTANCE 1,\n"
        "                    list 4 = refined around MOTOR_INDUCTANCE 1, PID 255/60)\n"
        "                   any input aborts\n"
        "  run <L> <P> <I> [speed] [seconds]\n"
        "                   one run with MOTOR_INDUCTANCE/PID_P/PID_I (defaults 0.3, 10 s), settings restored\n"
        "  profile up|start [L P I]\n"
        "                   up: speed steps 0.12 - 0.70 without stopping, start: 3 starts at 0.12 - 0.25\n"
        "                   default L 1, P 255, I 20. Settings restored. Any input aborts.\n"
        "  h                help\n"};

    // Measured on ford: chip electrical Hz per wheel revolution per second (1 pole pair, gear ~12).
    constexpr double estHzPerWheelRps{12.0};
    constexpr double wheelCircumferenceM{3.14159265 * 0.034}; // 34 mm wheel.
    constexpr double estLimitHz{1000.0};                      // Stay below the A89301 limit of about 1085 Hz.
    constexpr std::uint32_t profileSampleMs{250U};
    constexpr float profileUpDemands[]{0.12F, 0.15F, 0.20F, 0.25F, 0.30F, 0.35F, 0.40F,
                                       0.45F, 0.50F, 0.55F, 0.60F, 0.65F, 0.70F};
    constexpr float profileStartDemands[]{0.12F, 0.15F, 0.20F, 0.25F};
    constexpr std::uint32_t profileStartAttempts{3U};

    // PID (and optionally MOTOR_INDUCTANCE) combinations tested by the sweep command.
    constexpr std::uint8_t keepInductance{0xFFU};
    struct PidPair
    {
        std::uint8_t p;
        std::uint8_t i;
        std::uint8_t inductance{keepInductance}; // keepInductance = leave MOTOR_INDUCTANCE unchanged.
    };
    // List 1: broad sweep. The first entry is the original setting.
    constexpr PidPair sweepPairs1[]{
        {40U, 30U},
        {5U, 30U},  {10U, 30U}, {20U, 30U},  {80U, 30U},   {160U, 30U},  {255U, 30U},
        {40U, 5U},  {40U, 15U}, {40U, 60U},  {40U, 120U},  {40U, 255U},
        {10U, 10U}, {80U, 80U}, {160U, 160U}, {255U, 255U},
    };
    // List 2: refined around the combinations that ran in list 1.
    constexpr PidPair sweepPairs2[]{
        {255U, 255U}, {160U, 160U}, {200U, 200U}, {120U, 120U},
        {255U, 160U}, {160U, 255U}, {255U, 120U}, {120U, 255U},
        {5U, 30U},    {10U, 10U},   {5U, 5U},     {5U, 160U},  {10U, 255U},
    };
    // List 3: PID with MOTOR_INDUCTANCE 1, and the stable inductance 0 setting as reference.
    constexpr PidPair sweepPairs3[]{
        {160U, 160U, 0U},
        {60U, 60U, 1U},   {80U, 80U, 1U},   {120U, 120U, 1U}, {160U, 160U, 1U}, {200U, 200U, 1U},
        {255U, 255U, 1U}, {255U, 120U, 1U}, {120U, 255U, 1U}, {255U, 60U, 1U},  {60U, 255U, 1U},
    };
    // List 4: refined around MOTOR_INDUCTANCE 1, PID_P 255, PID_I 60 (best in list 3), which is repeated.
    constexpr PidPair sweepPairs4[]{
        {255U, 60U, 1U},
        {255U, 20U, 1U}, {255U, 40U, 1U}, {255U, 80U, 1U}, {255U, 100U, 1U},
        {200U, 40U, 1U}, {200U, 60U, 1U}, {200U, 80U, 1U},
        {160U, 40U, 1U}, {160U, 60U, 1U},
        {255U, 60U, 1U},
    };
    constexpr std::size_t sweepMaxCount{16U};
    constexpr std::uint32_t sweepPauseMs{3000U};   // Brake time before each run.
    constexpr std::uint32_t sweepSampleMs{500U};   // Status line period during a run.
    constexpr std::uint32_t sweepMaxRunS{30U};     // Longest allowed run time.
    constexpr float sweepRunningRps{2.0F};         // Wheel speed in the last second that counts as running.

    driver::serial::Esp32s3 serial(driver::serial::Config{
        .port = UART_NUM_0,
        .txPin = UART_PIN_NO_CHANGE,
        .rxPin = UART_PIN_NO_CHANGE,
        .baudRate = 115200,
        .rxBufSize = 256U,
        .useUsbJtag = true,
    });
    serial.connect();

    // Brake on first so the motor cannot start when VIN is powered.
    driver::gpio::Esp32s3 brakeGpio(brakePin, driver::gpio::Direction::Output);
    brakeGpio.write(true);
    driver::gpio::Esp32s3 directionGpio(directionPin, driver::gpio::Direction::Output);
    directionGpio.write(true);

    driver::i2c::Esp32s3 i2c(driver::i2c::Config{.sdaPin = sdaPin, .sclPin = sclPin});
    a89301::Programmer programmer(i2c, [](const std::uint32_t ms) { esp_rom_delay_us(ms * 1000U); });

    driver::gpio::Esp32s3 odometerGpio(odometerPin, driver::gpio::Direction::InputPullup);
    driver::odometer::A3144 odometer(odometerGpio, driver::odometer::Config{.pulsesPerRevolution = wheelPulsesPerRev});

    char buf[200]{'\0'};
    serial.write("\nA89301 config: SDA GPIO11 (A4), SCL GPIO12 (A5), DIR GPIO7 (D4), BRAKE GPIO5 (D2), "
                 "odometer GPIO18 (D9)\n");
    serial.write(brakeGpio.isInitialized() && directionGpio.isInitialized() ? "GPIO init OK, brake ON\n"
                                                                            : "GPIO init FAILED\n");
    serial.write(i2c.init() ? "I2C init OK\n" : "I2C init FAILED\n");
    serial.write(odometer.init() ? "Odometer init OK\n" : "Odometer init FAILED\n");

    driver::adc::Esp32s3 motorTempAdc(motorTempPin);
    serial.write(motorTempAdc.init() ? "Motor temperature ADC init OK (TMP36 on A0)\n"
                                     : "Motor temperature ADC init FAILED\n");

    // TMP36: 0.5 V at 0 degC, 10 mV/degC. Averaged over 8 samples. NaN if the reading is not plausible.
    auto motorTempC = [&]() {
        constexpr int samples{8};
        float sum{0.0F};
        for (int n{0}; n < samples; ++n)
        {
            const float volts{motorTempAdc.readVoltage()};
            if (std::isnan(volts)) { return std::numeric_limits<float>::quiet_NaN(); }
            sum += volts;
        }
        const float tempC{(sum / samples - 0.5F) * 100.0F};
        return ((tempC < -20.0F) || (tempC > 125.0F)) ? std::numeric_limits<float>::quiet_NaN() : tempC;
    };

    // Brake on and speed demand 0.
    auto stopMotor = [&]() {
        brakeGpio.write(true);
        programmer.setSpeedDemand(0U);
    };

    {
        const float tempC{motorTempC()};
        std::snprintf(buf, sizeof(buf), std::isnan(tempC) ? "Motor temperature: NO VALID READING (check TMP36)\n"
                                                          : "Motor temperature: %.1f degC\n",
                      static_cast<double>(tempC));
        serial.write(buf);
    }
    serial.write("Power VIN, then type p to probe.\n");
    serial.write(helpText);

    // Read one EEPROM word and its working register.
    auto readPair = [&](const std::uint8_t address, std::uint16_t& eeprom, std::uint16_t& working) {
        return programmer.readRegister(address, eeprom)
               && programmer.readRegister(static_cast<std::uint8_t>(address + a89301::WorkingRegisterOffset),
                                          working);
    };

    // Wait, but return true early if the user typed a line (the line is consumed).
    auto waitOrAbort = [&](const std::uint32_t ms) {
        for (std::uint32_t waited{0U}; waited < ms; waited += pollPeriodMs)
        {
            vTaskDelay(pdMS_TO_TICKS(pollPeriodMs));
            if (serial.isDataAvailable())
            {
                char discard[64]{'\0'};
                serial.read(discard, sizeof(discard));
                return true;
            }
        }
        return false;
    };

    // Run each PID combination at the given speed for runMs and print one result line each.
    auto runSweep = [&](const float speed, const std::uint32_t runMs, const PidPair* pairs, const std::size_t count) {
        const a89301::Field* pField{a89301::findField("PID_P")};
        const a89301::Field* iField{a89301::findField("PID_I")};
        const a89301::Field* lField{a89301::findField("MOTOR_INDUCTANCE")};
        std::uint16_t originalP{}, originalI{}, originalL{}, sense{};
        if (!programmer.readField(*pField, true, originalP) || !programmer.readField(*iField, true, originalI)
            || !programmer.readField(*lField, true, originalL)
            || !programmer.readField(*a89301::findField("SENSE_RESISTOR"), true, sense) || (sense == 0U))
        {
            serial.write("I2C read failed, sweep not started\n");
            return;
        }
        const double mAPerLsb{125.0 / sense};

        const auto demand{static_cast<std::uint16_t>(speed * a89301::SpeedDemandMax + 0.5F)};
        static char results[sweepMaxCount][160]{}; // Static to keep it off the main task stack.
        std::size_t done{0U};
        bool aborted{false};

        std::snprintf(buf, sizeof(buf), "Sweep: %u combinations at speed %.2f, %lu s each. Type anything to abort.\n",
                      static_cast<unsigned>(count), static_cast<double>(speed),
                      static_cast<unsigned long>((sweepPauseMs + runMs) / 1000U));
        serial.write(buf);

        for (std::size_t n{0U}; (n < count) && (n < sweepMaxCount); ++n)
        {
            const PidPair& pair{pairs[n]};
            stopMotor();
            if (waitOrAbort(sweepPauseMs)) { aborted = true; break; }

            // Wait until the motor has cooled down. Without a valid temperature the sweep stops.
            float tempC{motorTempC()};
            while (!std::isnan(tempC) && (tempC > coolMotorTempC))
            {
                std::snprintf(buf, sizeof(buf), "  cooling: motor %.1f degC, waiting for < %.0f degC\n",
                              static_cast<double>(tempC), static_cast<double>(coolMotorTempC));
                serial.write(buf);
                if (waitOrAbort(5000U)) { aborted = true; break; }
                tempC = motorTempC();
            }
            if (aborted) { break; }
            if (std::isnan(tempC))
            {
                serial.write("No valid motor temperature, sweep aborted (check TMP36 on A0)\n");
                aborted = true;
                break;
            }

            const auto inductance{static_cast<std::uint16_t>((pair.inductance == keepInductance) ? originalL
                                                                                                : pair.inductance)};
            if (!programmer.writeField(*pField, pair.p) || !programmer.writeField(*iField, pair.i)
                || !programmer.writeField(*lField, inductance))
            {
                serial.write("I2C write failed, sweep aborted\n");
                aborted = true;
                break;
            }

            std::snprintf(buf, sizeof(buf), "--- PID_P %u, PID_I %u, MOTOR_INDUCTANCE %u\n", pair.p, pair.i,
                          inductance);
            serial.write(buf);
            if (!programmer.setSpeedDemand(demand))
            {
                serial.write("I2C write failed, sweep aborted\n");
                aborted = true;
                break;
            }
            brakeGpio.write(false);

            float maxWheelRps{0.0F};
            std::uint32_t previousPulses{odometer.pulseCount()};
            std::uint32_t lastSecondPulses{previousPulses};
            double lastSpeedHz{0.0};
            std::uint16_t lastState{0U};
            bool spinning{false};   // True once the chip has reached the spinning state.
            bool restarted{false};  // True if the chip left the spinning state again (failure and restart).
            bool stalled{false};    // True if the chip drove a wheel that did not turn.
            bool tooHot{false};     // True if the motor exceeded maxMotorTempC.
            std::int64_t lastPulseUs{esp_timer_get_time()};
            bool wasFast{false};
            double busSumMa{0.0}, motorSumMa{0.0}; // Currents summed over the last 2 s of the run.
            std::uint32_t currentSamples{0U};

            for (std::uint32_t elapsed{sweepSampleMs}; elapsed <= runMs; elapsed += sweepSampleMs)
            {
                if (waitOrAbort(sweepSampleMs)) { aborted = true; break; }

                const std::uint32_t pulses{odometer.pulseCount()};
                if (pulses != previousPulses) { lastPulseUs = esp_timer_get_time(); }
                const float wheelRps{(pulses - previousPulses) * 1000.0F / sweepSampleMs / wheelPulsesPerRev};
                previousPulses = pulses;
                if (wheelRps > maxWheelRps) { maxWheelRps = wheelRps; }
                if (elapsed == (runMs - 1000U)) { lastSecondPulses = pulses; }

                std::uint16_t speedReg{}, command{}, state{}, busReg{}, motorReg{};
                programmer.readRegister(a89301::readback::MotorSpeed, speedReg);
                programmer.readRegister(a89301::readback::ControlCommand, command);
                programmer.readRegister(a89301::readback::OperationState, state);
                programmer.readRegister(a89301::readback::BusCurrent, busReg);
                programmer.readRegister(a89301::readback::QCurrent, motorReg);
                lastSpeedHz = speedReg * 0.530;
                lastState   = static_cast<std::uint16_t>(state >> 12U);
                const double busMa{busReg * mAPerLsb};
                const double motorMa{static_cast<std::int16_t>(motorReg) * mAPerLsb};
                if ((elapsed + 2000U) > runMs)
                {
                    busSumMa += busMa;
                    motorSumMa += motorMa;
                    ++currentSamples;
                }

                constexpr std::uint16_t spinningState{3U};
                if (lastState == spinningState) { spinning = true; }
                else if (spinning) { restarted = true; }

                tempC = motorTempC();
                std::snprintf(buf, sizeof(buf),
                              "  t %4.1f s | est %6.1f Hz | wheel %4.1f rps | cmd %4.1f %% | bus %5.0f mA | "
                              "motor %5.0f mA | %s | %.1f degC\n",
                              elapsed / 1000.0, lastSpeedHz, static_cast<double>(wheelRps),
                              command * 100.0 / 511.0, busMa, motorMa,
                              a89301::stateName(static_cast<std::uint8_t>(lastState)), static_cast<double>(tempC));
                serial.write(buf);

                // Safety: stop on overtemperature (whole sweep) or when the chip drives a wheel that stands still.
                if (std::isnan(tempC) || (tempC > maxMotorTempC))
                {
                    tooHot = true;
                    break;
                }
                // The stall window starts when the estimate first passes stallMinSpeedHz.
                const bool fast{(lastState == spinningState) && (lastSpeedHz > stallMinSpeedHz)};
                if (fast && !wasFast) { lastPulseUs = esp_timer_get_time(); }
                wasFast = fast;
                if (fast && ((esp_timer_get_time() - lastPulseUs) > stallTimeoutUs))
                {
                    stalled = true;
                    break;
                }
            }
            stopMotor();
            if (aborted) { break; }
            if (tooHot)
            {
                std::snprintf(buf, sizeof(buf), "MOTOR TOO HOT (%.1f degC) or no valid temperature, sweep stopped\n",
                              static_cast<double>(tempC));
                serial.write(buf);
                aborted = true;
                break;
            }

            // Running means: never left the spinning state and the wheel still turns in the last second.
            const float endWheelRps{stalled ? 0.0F
                                            : (odometer.pulseCount() - lastSecondPulses) / 1.0F / wheelPulsesPerRev};
            const char* verdict{stalled                            ? "stalled"
                                : restarted                        ? "restarted"
                                : (endWheelRps >= sweepRunningRps) ? "RUNNING"
                                                                   : "stopped"};
            // Estimate per wheel revolution: about 12 when the chip tracks the real rotor.
            const double ratio{(endWheelRps > 0.0F) ? (lastSpeedHz / endWheelRps) : 0.0};
            const double busAvgMa{(currentSamples > 0U) ? (busSumMa / currentSamples) : 0.0};
            const double motorAvgMa{(currentSamples > 0U) ? (motorSumMa / currentSamples) : 0.0};
            std::snprintf(results[done], sizeof(results[done]),
                          "RESULT L %u P %3u I %3u | max wheel %4.1f | end wheel %4.1f rps | est %6.1f Hz | "
                          "ratio %5.1f | bus %5.0f mA | motor %5.0f mA | %s\n",
                          inductance, pair.p, pair.i, static_cast<double>(maxWheelRps),
                          static_cast<double>(endWheelRps), lastSpeedHz, ratio, busAvgMa, motorAvgMa, verdict);
            serial.write(results[done]);
            ++done;
        }

        brakeGpio.write(true);
        programmer.setSpeedDemand(0U);
        const bool restored{programmer.writeField(*pField, originalP) && programmer.writeField(*iField, originalI)
                            && programmer.writeField(*lField, originalL)};
        std::snprintf(buf, sizeof(buf), "%s Brake ON. PID_P %u, PID_I %u, MOTOR_INDUCTANCE %u %s.\n",
                      aborted ? "Sweep ABORTED." : "Sweep done.", originalP, originalI, originalL,
                      restored ? "restored" : "NOT restored (I2C error, power cycle VIN)");
        serial.write(buf);

        serial.write("\nSummary:\n");
        for (std::size_t n{0U}; n < done; ++n) { serial.write(results[n]); }
    };

    // Motor settings that run and profile change and restore afterwards.
    struct MotorConfig
    {
        std::uint16_t inductance;
        std::uint16_t p;
        std::uint16_t i;
    };
    const a89301::Field* inductanceField{a89301::findField("MOTOR_INDUCTANCE")};
    const a89301::Field* pidPField{a89301::findField("PID_P")};
    const a89301::Field* pidIField{a89301::findField("PID_I")};

    auto readConfig = [&](MotorConfig& config) {
        return programmer.readField(*inductanceField, true, config.inductance)
               && programmer.readField(*pidPField, true, config.p)
               && programmer.readField(*pidIField, true, config.i);
    };
    auto writeConfig = [&](const MotorConfig& config) {
        return programmer.writeField(*inductanceField, config.inductance)
               && programmer.writeField(*pidPField, config.p)
               && programmer.writeField(*pidIField, config.i);
    };

    // Wait until the motor is below coolMotorTempC. Returns false if aborted or without a valid temperature.
    auto waitCool = [&]() {
        float tempC{motorTempC()};
        while (!std::isnan(tempC) && (tempC > coolMotorTempC))
        {
            std::snprintf(buf, sizeof(buf), "  cooling: motor %.1f degC, waiting for < %.0f degC\n",
                          static_cast<double>(tempC), static_cast<double>(coolMotorTempC));
            serial.write(buf);
            if (waitOrAbort(5000U)) { return false; }
            tempC = motorTempC();
        }
        if (std::isnan(tempC))
        {
            serial.write("No valid motor temperature (check TMP36 on A0)\n");
            return false;
        }
        return true;
    };

    // Averages over the measurement window of one drive() call, plus what stopped it early.
    struct DriveStats
    {
        double estAvgHz{0.0};
        double estMinHz{0.0};
        double estMaxHz{0.0};
        double wheelRps{0.0};
        double busMa{0.0};
        double vbb{0.0};
        double motorMa{0.0};
        double cmdPct{0.0};
        float tempC{0.0F};
        bool restarted{false};
        bool stalled{false};
        bool tooHot{false};
        bool overLimit{false};
        bool aborted{false};
    };

    // Drive at speed for holdMs (from standstill or from the current speed) and average the last measureMs.
    // Stops the motor on abort, overtemperature, speed limit, stall or restart.
    auto drive = [&](const float speed, const std::uint32_t holdMs, const std::uint32_t measureMs,
                     const bool fromStandstill) {
        DriveStats stats{};
        std::uint16_t sense{};
        const auto demand{static_cast<std::uint16_t>(speed * a89301::SpeedDemandMax + 0.5F)};
        if (!programmer.readField(*a89301::findField("SENSE_RESISTOR"), true, sense) || (sense == 0U)
            || !programmer.setSpeedDemand(demand))
        {
            serial.write("I2C error\n");
            stopMotor();
            stats.aborted = true;
            return stats;
        }
        brakeGpio.write(false);
        const double mAPerLsb{125.0 / sense};

        bool spinning{!fromStandstill};
        bool wasFast{false};
        std::uint32_t previousPulses{odometer.pulseCount()};
        std::int64_t lastPulseUs{esp_timer_get_time()};
        std::uint32_t windowPulses{0U};
        std::int64_t windowStartUs{0};
        std::uint32_t samples{0U};
        double estSum{0.0}, busSum{0.0}, vbbSum{0.0}, motorSum{0.0}, cmdSum{0.0};
        stats.estMinHz = 1.0e9;

        for (std::uint32_t elapsed{profileSampleMs}; elapsed <= holdMs; elapsed += profileSampleMs)
        {
            if (waitOrAbort(profileSampleMs))
            {
                stats.aborted = true;
                break;
            }

            const std::int64_t nowUs{esp_timer_get_time()};
            const std::uint32_t pulses{odometer.pulseCount()};
            if (pulses != previousPulses)
            {
                previousPulses = pulses;
                lastPulseUs    = nowUs;
            }

            std::uint16_t speedReg{}, command{}, state{}, busReg{}, motorReg{}, vbbReg{};
            programmer.readRegister(a89301::readback::MotorSpeed, speedReg);
            programmer.readRegister(a89301::readback::ControlCommand, command);
            programmer.readRegister(a89301::readback::OperationState, state);
            programmer.readRegister(a89301::readback::BusCurrent, busReg);
            programmer.readRegister(a89301::readback::QCurrent, motorReg);
            programmer.readRegister(a89301::readback::Vbb, vbbReg);
            const double estHz{speedReg * 0.530};
            const auto stateNr{static_cast<std::uint8_t>(state >> 12U)};
            const double busMa{busReg * mAPerLsb};
            const double motorMa{static_cast<std::int16_t>(motorReg) * mAPerLsb};
            const double cmdPct{command * 100.0 / 511.0};
            stats.tempC = motorTempC();

            constexpr std::uint8_t spinningState{3U};
            if (stateNr == spinningState) { spinning = true; }
            else if (spinning) { stats.restarted = true; }

            if ((elapsed + measureMs) > holdMs)
            {
                if (samples == 0U)
                {
                    windowPulses  = pulses;
                    windowStartUs = nowUs;
                }
                ++samples;
                estSum += estHz;
                busSum += busMa;
                vbbSum += vbbReg / 5.0;
                motorSum += motorMa;
                cmdSum += cmdPct;
                if (estHz < stats.estMinHz) { stats.estMinHz = estHz; }
                if (estHz > stats.estMaxHz) { stats.estMaxHz = estHz; }
            }

            if ((elapsed % 500U) == 0U)
            {
                std::snprintf(buf, sizeof(buf),
                              "  %.2f t %4.1f s | est %5.0f Hz | cmd %4.1f %% | bus %4.0f mA | motor %5.0f mA | "
                              "%s | %.1f degC\n",
                              static_cast<double>(speed), elapsed / 1000.0, estHz, cmdPct, busMa, motorMa,
                              a89301::stateName(stateNr), static_cast<double>(stats.tempC));
                serial.write(buf);
            }

            // Safety and limits.
            if (std::isnan(stats.tempC) || (stats.tempC > maxMotorTempC)) { stats.tooHot = true; }
            if (estHz > estLimitHz) { stats.overLimit = true; }
            const bool fast{(stateNr == spinningState) && (estHz > stallMinSpeedHz)};
            if (fast && !wasFast) { lastPulseUs = nowUs; }
            wasFast = fast;
            if (fast && ((nowUs - lastPulseUs) > stallTimeoutUs)) { stats.stalled = true; }
            if (stats.tooHot || stats.overLimit || stats.stalled || stats.restarted) { break; }
        }

        if (samples > 0U)
        {
            stats.estAvgHz = estSum / samples;
            stats.busMa    = busSum / samples;
            stats.vbb      = vbbSum / samples;
            stats.motorMa  = motorSum / samples;
            stats.cmdPct   = cmdSum / samples;
            const double windowS{(esp_timer_get_time() - windowStartUs) / 1.0e6};
            if (windowS > 0.0) { stats.wheelRps = (odometer.pulseCount() - windowPulses) / windowS / wheelPulsesPerRev; }
        }
        else { stats.estMinHz = 0.0; }

        if (stats.aborted || stats.tooHot || stats.overLimit || stats.stalled || stats.restarted) { stopMotor(); }
        return stats;
    };

    // True if a drive() result counts as steady running.
    auto isSteady = [](const DriveStats& s) {
        const double spreadPct{(s.estAvgHz > 0.0) ? ((s.estMaxHz - s.estMinHz) * 100.0 / s.estAvgHz) : 100.0};
        return !s.aborted && !s.tooHot && !s.overLimit && !s.stalled && !s.restarted && (spreadPct <= 15.0);
    };

    // One table line for a drive() result.
    auto formatStats = [&](char* out, const std::size_t size, const char* label, const float speed,
                           const DriveStats& s) {
        const double spreadPct{(s.estAvgHz > 0.0) ? ((s.estMaxHz - s.estMinHz) * 100.0 / s.estAvgHz) : 0.0};
        const char* verdict{s.aborted     ? "aborted"
                            : s.tooHot    ? "TOO HOT"
                            : s.overLimit ? "LIMIT"
                            : s.stalled   ? "stalled"
                            : s.restarted ? "restarted"
                            : isSteady(s) ? "steady"
                                          : "unsteady"};
        std::snprintf(out, size,
                      "%s %.2f | est %4.0f Hz +-%3.0f%% | %4.1f rps | %4.2f m/s odo | %4.2f m/s est | "
                      "%4.0f mA %4.1f V %4.2f W | motor %5.0f mA | cmd %4.1f %% | %4.1f degC | %s\n",
                      label, static_cast<double>(speed), s.estAvgHz, spreadPct / 2.0, s.wheelRps,
                      s.wheelRps * wheelCircumferenceM, s.estAvgHz / estHzPerWheelRps * wheelCircumferenceM,
                      s.busMa, s.vbb, s.busMa * s.vbb / 1000.0, s.motorMa, s.cmdPct,
                      static_cast<double>(s.tempC), verdict);
    };

    // run: one drive with the given settings, then restore the previous settings.
    auto runOnce = [&](const MotorConfig& config, const float speed, const std::uint32_t holdMs) {
        MotorConfig original{};
        if (!readConfig(original) || !writeConfig(config))
        {
            serial.write("I2C error, run not started\n");
            return;
        }
        std::snprintf(buf, sizeof(buf), "Run: MOTOR_INDUCTANCE %u, PID_P %u, PID_I %u, speed %.2f, %lu s\n",
                      config.inductance, config.p, config.i, static_cast<double>(speed),
                      static_cast<unsigned long>(holdMs / 1000U));
        serial.write(buf);

        if (waitCool())
        {
            const DriveStats stats{drive(speed, holdMs, (holdMs > 6000U) ? 3000U : holdMs / 2U, true)};
            stopMotor();
            serial.write("                         est             wheel    odometer      chip est       supply"
                         "                  motor       command\n");
            char line[200]{};
            formatStats(line, sizeof(line), "RUN", speed, stats);
            serial.write(line);
        }
        stopMotor();
        serial.write(writeConfig(original) ? "Brake ON, previous settings restored.\n"
                                           : "Brake ON, settings NOT restored (I2C error, power cycle VIN)\n");
    };

    // profile up: speed steps without stopping. profile start: repeated starts from standstill.
    auto runProfile = [&](const bool up, const MotorConfig& config) {
        MotorConfig original{};
        if (!readConfig(original) || !writeConfig(config))
        {
            serial.write("I2C error, profile not started\n");
            return;
        }
        std::snprintf(buf, sizeof(buf), "Profile %s: MOTOR_INDUCTANCE %u, PID_P %u, PID_I %u. Any input aborts.\n",
                      up ? "up" : "start", config.inductance, config.p, config.i);
        serial.write(buf);

        constexpr std::size_t maxLines{13U};
        static char lines[maxLines][200]{}; // Static to keep it off the main task stack.
        std::size_t done{0U};
        bool stop{false};

        if (up)
        {
            // Hold each step 5 s and average the last 3 s. Stop at the first failing step.
            stop = !waitCool();
            for (std::size_t n{0U}; !stop && (n < (sizeof(profileUpDemands) / sizeof(profileUpDemands[0]))); ++n)
            {
                const DriveStats stats{drive(profileUpDemands[n], 5000U, 3000U, n == 0U)};
                formatStats(lines[done], sizeof(lines[done]), "UP", profileUpDemands[n], stats);
                serial.write(lines[done]);
                ++done;
                stop = stats.aborted || stats.tooHot || stats.overLimit || stats.stalled || stats.restarted;
            }
        }
        else
        {
            // Start from standstill profileStartAttempts times per speed, drive 6 s, average the last 3 s.
            for (std::size_t n{0U}; !stop && (n < (sizeof(profileStartDemands) / sizeof(profileStartDemands[0])));
                 ++n)
            {
                std::uint32_t steady{0U};
                DriveStats last{};
                for (std::uint32_t attempt{1U}; !stop && (attempt <= profileStartAttempts); ++attempt)
                {
                    stopMotor();
                    if (waitOrAbort(3000U) || !waitCool())
                    {
                        stop = true;
                        break;
                    }
                    last = drive(profileStartDemands[n], 6000U, 3000U, true);
                    stopMotor();
                    char label[16]{};
                    std::snprintf(label, sizeof(label), "START #%lu", static_cast<unsigned long>(attempt));
                    char line[200]{};
                    formatStats(line, sizeof(line), label, profileStartDemands[n], last);
                    serial.write(line);
                    if (isSteady(last)) { ++steady; }
                    stop = last.aborted || last.tooHot;
                }
                std::snprintf(lines[done], sizeof(lines[done]), "START %.2f: %lu of %lu steady, last: %.2f m/s est, %.2f W\n",
                              static_cast<double>(profileStartDemands[n]), static_cast<unsigned long>(steady),
                              static_cast<unsigned long>(profileStartAttempts),
                              last.estAvgHz / estHzPerWheelRps * wheelCircumferenceM, last.busMa * last.vbb / 1000.0);
                ++done;
            }
        }

        stopMotor();
        serial.write(writeConfig(original) ? "Brake ON, previous settings restored.\n"
                                           : "Brake ON, settings NOT restored (I2C error, power cycle VIN)\n");
        serial.write("\nProfile summary (est +- = half the min-max spread, m/s est assumes 12 Hz per wheel rps):\n");
        for (std::size_t n{0U}; n < done; ++n) { serial.write(lines[n]); }
    };

    // Wheel speed is averaged over the last wheelWindow monitor samples (1 pulse per revolution is coarse).
    constexpr std::size_t wheelWindow{5U};
    std::uint32_t wheelCounts[wheelWindow]{};
    std::int64_t wheelTimesUs[wheelWindow]{};
    std::size_t wheelIndex{0U};

    bool monitor{false};
    TickType_t lastMonitor{xTaskGetTickCount()};
    std::int64_t startUs{esp_timer_get_time()}; // Time reference for the monitor, reset by spd.

    // Safety supervision of manual spd runs: overtemperature and stalled wheel.
    bool driving{false};
    TickType_t lastSafety{xTaskGetTickCount()};
    std::uint32_t safetyPulses{0U};
    std::int64_t safetyLastPulseUs{0};
    bool safetyWasFast{false};

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(pollPeriodMs));

        if (driving && ((xTaskGetTickCount() - lastSafety) >= pdMS_TO_TICKS(safetyPeriodMs)))
        {
            lastSafety = xTaskGetTickCount();
            const std::int64_t nowUs{esp_timer_get_time()};
            const std::uint32_t pulses{odometer.pulseCount()};
            if (pulses != safetyPulses)
            {
                safetyPulses      = pulses;
                safetyLastPulseUs = nowUs;
            }

            std::uint16_t speedReg{}, state{};
            const bool ok{programmer.readRegister(a89301::readback::MotorSpeed, speedReg)
                          && programmer.readRegister(a89301::readback::OperationState, state)};
            // The stall window starts when the estimate first passes stallMinSpeedHz.
            const bool fast{ok && ((state >> 12U) == 3U) && ((speedReg * 0.530) > stallMinSpeedHz)};
            if (fast && !safetyWasFast) { safetyLastPulseUs = nowUs; }
            safetyWasFast = fast;
            const bool stalled{fast && ((nowUs - safetyLastPulseUs) > stallTimeoutUs)};
            const float tempC{motorTempC()};
            const bool tooHot{!std::isnan(tempC) && (tempC > maxMotorTempC)};

            if (stalled || tooHot)
            {
                stopMotor();
                driving = false;
                std::snprintf(buf, sizeof(buf), "SAFETY STOP: %s (motor %.1f degC). Brake ON, speed demand 0.\n",
                              stalled ? "chip drives but the wheel does not turn" : "motor too hot",
                              static_cast<double>(tempC));
                serial.write(buf);
            }
        }

        if (monitor && ((xTaskGetTickCount() - lastMonitor) >= pdMS_TO_TICKS(monitorPeriodMs)))
        {
            lastMonitor = xTaskGetTickCount();
            const std::int64_t nowUs{esp_timer_get_time()};
            const std::uint32_t pulses{odometer.pulseCount()};

            // Wheel revolutions per second over the window (oldest sample is the next to be overwritten).
            const std::size_t oldest{(wheelIndex + 1U) % wheelWindow};
            const std::int64_t windowUs{nowUs - wheelTimesUs[oldest]};
            const double wheelRps{(wheelTimesUs[oldest] != 0) && (windowUs > 0)
                                      ? (pulses - wheelCounts[oldest]) * 1.0e6 / windowUs / wheelPulsesPerRev
                                      : 0.0};
            wheelCounts[wheelIndex]  = pulses;
            wheelTimesUs[wheelIndex] = nowUs;
            wheelIndex               = (wheelIndex + 1U) % wheelWindow;

            std::uint16_t speed{}, busCurrent{}, motorCurrent{}, vbb{}, demand{}, command{}, state{}, sense{};
            const bool ok{programmer.readRegister(a89301::readback::MotorSpeed, speed)
                          && programmer.readRegister(a89301::readback::BusCurrent, busCurrent)
                          && programmer.readRegister(a89301::readback::QCurrent, motorCurrent)
                          && programmer.readRegister(a89301::readback::Vbb, vbb)
                          && programmer.readRegister(a89301::readback::ControlDemand, demand)
                          && programmer.readRegister(a89301::readback::ControlCommand, command)
                          && programmer.readRegister(a89301::readback::OperationState, state)
                          && programmer.readField(*a89301::findField("SENSE_RESISTOR"), true, sense)};
            if (!ok)
            {
                serial.write("monitor: I2C read failed\n");
                continue;
            }

            // Q-axis current can be negative (braking), so it is read as a signed value.
            const double mAPerLsb{(sense != 0U) ? (125.0 / sense) : 0.0};
            std::snprintf(buf, sizeof(buf),
                          "t %6.2f s | speed %6.1f Hz | wheel %5.1f rps (%lu) | bus %5.0f mA | motor %6.0f mA | "
                          "VBB %4.1f V | demand %4.1f %% | cmd %4.1f %% | state %2u %s (0x%04X) | %.1f degC\n",
                          (nowUs - startUs) / 1.0e6, speed * 0.530, wheelRps, static_cast<unsigned long>(pulses),
                          busCurrent * mAPerLsb, static_cast<std::int16_t>(motorCurrent) * mAPerLsb, vbb / 5.0,
                          demand * 100.0 / 511.0, command * 100.0 / 511.0, state >> 12U,
                          a89301::stateName(static_cast<std::uint8_t>(state >> 12U)), state,
                          static_cast<double>(motorTempC()));
            serial.write(buf);
        }

        if (!serial.isDataAvailable()) { continue; }

        char line[64]{'\0'};
        if (serial.read(line, sizeof(line)) == 0U) { continue; }

        const char* cmd{std::strtok(line, " ")};
        const char* arg1{std::strtok(nullptr, " ")};
        const char* arg2{std::strtok(nullptr, " ")};
        if (cmd == nullptr) { continue; }

        if (std::strcmp(cmd, "p") == 0)
        {
            serial.write(programmer.isPresent() ? "A89301 found at 0x55\n"
                                                : "No answer at 0x55 (VIN on? wiring? pull-up?)\n");
        }
        else if (std::strcmp(cmd, "d") == 0)
        {
            serial.write("addr  EEPROM  working\n");
            for (std::uint8_t address{a89301::EepromFirst}; address <= a89301::EepromLast; ++address)
            {
                std::uint16_t eeprom{}, working{};
                if (!readPair(address, eeprom, working))
                {
                    serial.write("I2C read failed\n");
                    break;
                }
                std::snprintf(buf, sizeof(buf), "%4u  0x%04X  0x%04X%s%s\n", address, eeprom, working,
                              (eeprom != working) ? "  <- differs" : "",
                              a89301::hasDefaultAllegroBits(address, working) ? ""
                                                                              : "  <- ALLEGRO-ONLY BITS CHANGED");
                serial.write(buf);
            }

            serial.write("\nfield                      EEPROM  working  note\n");
            for (const auto& field : a89301::Fields)
            {
                std::uint16_t eeprom{}, working{};
                if (!programmer.readField(field, false, eeprom) || !programmer.readField(field, true, working))
                {
                    serial.write("I2C read failed\n");
                    break;
                }
                std::snprintf(buf, sizeof(buf), "%-26s %6u  %7u  %s\n", field.name, eeprom, working, field.note);
                serial.write(buf);
            }

            std::uint16_t ratedSpeed{}, ratedCurrent{}, ratedVoltage{}, sense{};
            if (programmer.readField(*a89301::findField("RATED_SPEED"), true, ratedSpeed)
                && programmer.readField(*a89301::findField("RATED_CURRENT"), true, ratedCurrent)
                && programmer.readField(*a89301::findField("RATED_VOLTAGE"), true, ratedVoltage)
                && programmer.readField(*a89301::findField("SENSE_RESISTOR"), true, sense))
            {
                std::snprintf(buf, sizeof(buf),
                              "\nworking: rated speed %.1f Hz, rated current %.0f mA, rated voltage %.1f V, "
                              "sense resistor %.2f mOhm\n",
                              ratedSpeed * 0.530, (sense != 0U) ? (ratedCurrent * 125.0 / sense) : 0.0,
                              ratedVoltage / 5.0, sense / 3.7);
                serial.write(buf);
            }
        }
        else if ((std::strcmp(cmd, "r") == 0) && (arg1 != nullptr))
        {
            const auto reg{static_cast<std::uint8_t>(std::strtoul(arg1, nullptr, 0))};
            std::uint16_t value{};
            if (programmer.readRegister(reg, value))
            {
                std::snprintf(buf, sizeof(buf), "reg %u = 0x%04X (%u)\n", reg, value, value);
                serial.write(buf);
            }
            else { serial.write("I2C read failed\n"); }
        }
        else if (std::strcmp(cmd, "m") == 0)
        {
            monitor = !monitor;
            serial.write(monitor ? "Monitor ON\n" : "Monitor OFF\n");
        }
        else if ((std::strcmp(cmd, "set") == 0) && (arg1 != nullptr) && (arg2 != nullptr))
        {
            const a89301::Field* field{a89301::findField(arg1)};
            const auto value{static_cast<std::uint16_t>(std::strtoul(arg2, nullptr, 0))};
            std::uint16_t oldValue{};
            if (field == nullptr) { serial.write("Unknown field, type d to list fields\n"); }
            else if (!programmer.readField(*field, true, oldValue)) { serial.write("I2C read failed\n"); }
            else if (!programmer.writeField(*field, value))
            {
                serial.write("Write failed (value too large for the field, or I2C error)\n");
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "%s: %u -> %u (working register only, not saved)\n",
                              field->name, oldValue, value);
                serial.write(buf);
            }
        }
        else if ((std::strcmp(cmd, "spd") == 0) && (arg1 != nullptr))
        {
            const float speed{std::strtof(arg1, nullptr)};
            if ((speed < 0.0F) || (speed > 1.0F)) { serial.write("Speed must be in range 0.0 - 1.0\n"); }
            else
            {
                const auto demand{static_cast<std::uint16_t>(speed * a89301::SpeedDemandMax + 0.5F)};
                if (programmer.setSpeedDemand(demand))
                {
                    brakeGpio.write(false);
                    startUs = esp_timer_get_time();
                    driving = (demand > 0U);
                    safetyPulses      = odometer.pulseCount();
                    safetyLastPulseUs = startUs;
                    safetyWasFast     = false;
                    std::snprintf(buf, sizeof(buf), "I2C speed demand %u / 511, brake off\n", demand);
                    serial.write(buf);
                    if (std::isnan(motorTempC())) { serial.write("WARNING: no valid motor temperature\n"); }
                }
                else { serial.write("I2C write failed\n"); }
            }
        }
        else if (std::strcmp(cmd, "sweep") == 0)
        {
            const char* arg3{std::strtok(nullptr, " ")};
            const float speed{(arg1 != nullptr) ? std::strtof(arg1, nullptr) : 0.3F};
            const unsigned long seconds{(arg2 != nullptr) ? std::strtoul(arg2, nullptr, 0) : 5UL};
            const unsigned long list{(arg3 != nullptr) ? std::strtoul(arg3, nullptr, 0) : 1UL};

            if ((speed <= 0.0F) || (speed > 0.5F)) { serial.write("Sweep speed must be in range 0.0 - 0.5\n"); }
            else if ((seconds < 2UL) || (seconds > sweepMaxRunS)) { serial.write("Sweep seconds must be 2 - 30\n"); }
            else if ((list < 1UL) || (list > 4UL)) { serial.write("Sweep list must be 1 - 4\n"); }
            else
            {
                monitor = false;
                driving = false;
                const auto runMs{static_cast<std::uint32_t>(seconds * 1000UL)};
                if (list == 1UL) { runSweep(speed, runMs, sweepPairs1, sizeof(sweepPairs1) / sizeof(sweepPairs1[0])); }
                else if (list == 2UL) { runSweep(speed, runMs, sweepPairs2, sizeof(sweepPairs2) / sizeof(sweepPairs2[0])); }
                else if (list == 3UL) { runSweep(speed, runMs, sweepPairs3, sizeof(sweepPairs3) / sizeof(sweepPairs3[0])); }
                else { runSweep(speed, runMs, sweepPairs4, sizeof(sweepPairs4) / sizeof(sweepPairs4[0])); }
            }
        }
        else if ((std::strcmp(cmd, "run") == 0) || (std::strcmp(cmd, "profile") == 0))
        {
            const bool isRun{std::strcmp(cmd, "run") == 0};
            const char* arg3{std::strtok(nullptr, " ")};
            const char* arg4{std::strtok(nullptr, " ")};
            const char* arg5{std::strtok(nullptr, " ")};

            // run <L> <P> <I> [speed] [seconds] / profile up|start [L P I]
            const char* lArg{isRun ? arg1 : arg2};
            const char* pArg{isRun ? arg2 : arg3};
            const char* iArg{isRun ? arg3 : arg4};
            const MotorConfig config{
                static_cast<std::uint16_t>((lArg != nullptr) ? std::strtoul(lArg, nullptr, 0) : 1UL),
                static_cast<std::uint16_t>((pArg != nullptr) ? std::strtoul(pArg, nullptr, 0) : 255UL),
                static_cast<std::uint16_t>((iArg != nullptr) ? std::strtoul(iArg, nullptr, 0) : 20UL),
            };
            const float speed{(isRun && (arg4 != nullptr)) ? std::strtof(arg4, nullptr) : 0.3F};
            const unsigned long seconds{(isRun && (arg5 != nullptr)) ? std::strtoul(arg5, nullptr, 0) : 10UL};
            const bool profileUp{!isRun && (arg1 != nullptr) && (std::strcmp(arg1, "up") == 0)};
            const bool profileStart{!isRun && (arg1 != nullptr) && (std::strcmp(arg1, "start") == 0)};

            if (isRun && ((arg1 == nullptr) || (arg2 == nullptr) || (arg3 == nullptr)))
            {
                serial.write("Usage: run <L> <P> <I> [speed] [seconds]\n");
            }
            else if (!isRun && !profileUp && !profileStart) { serial.write("Usage: profile up|start [L P I]\n"); }
            else if ((config.inductance > 31U) || (config.p > 255U) || (config.i > 255U))
            {
                serial.write("L must be 0 - 31, P and I 0 - 255\n");
            }
            else if ((speed <= 0.0F) || (speed > 0.7F)) { serial.write("Speed must be in range 0.0 - 0.7\n"); }
            else if ((seconds < 2UL) || (seconds > sweepMaxRunS)) { serial.write("Seconds must be 2 - 30\n"); }
            else
            {
                monitor = false;
                driving = false;
                if (isRun) { runOnce(config, speed, static_cast<std::uint32_t>(seconds * 1000UL)); }
                else { runProfile(profileUp, config); }
            }
        }
        else if (std::strcmp(cmd, "stop") == 0)
        {
            driving = false;
            brakeGpio.write(true);
            serial.write(programmer.setSpeedDemand(0U) ? "Speed demand 0, brake ON\n"
                                                       : "Brake ON, but I2C write failed\n");
        }
        else if ((std::strcmp(cmd, "f") == 0) || (std::strcmp(cmd, "b") == 0))
        {
            const bool forward{std::strcmp(cmd, "f") == 0};
            directionGpio.write(forward);
            serial.write(forward ? "DIR high (forward)\n" : "DIR low (backward)\n");
        }
        else if (std::strcmp(cmd, "save") == 0)
        {
            const bool confirmed{(arg1 != nullptr) && (std::strcmp(arg1, "yes") == 0)};
            if (confirmed && !brakeGpio.read())
            {
                serial.write("Type stop before saving\n");
                continue;
            }

            std::uint8_t differences{0U};
            std::uint8_t failures{0U};
            for (std::uint8_t address{a89301::EepromFirst}; address <= a89301::EepromLast; ++address)
            {
                std::uint16_t eeprom{}, working{};
                if (!readPair(address, eeprom, working))
                {
                    serial.write("I2C read failed, nothing more is saved\n");
                    ++failures;
                    break;
                }
                if ((eeprom == working) || !a89301::isEepromWritable(address)) { continue; }

                ++differences;
                std::snprintf(buf, sizeof(buf), "addr %2u: EEPROM 0x%04X -> 0x%04X", address, eeprom, working);
                serial.write(buf);

                if (!confirmed) { serial.write("\n"); }
                else if (programmer.programEeprom(address, working)) { serial.write("  saved\n"); }
                else
                {
                    serial.write("  FAILED\n");
                    ++failures;
                }
            }

            if (differences == 0U) { serial.write("EEPROM already matches the working registers\n"); }
            else if (!confirmed) { serial.write("Type 'save yes' to program these words (keep VIN on)\n"); }
            else if (failures == 0U) { serial.write("Saved. Power cycle VIN to load the EEPROM.\n"); }
            else { serial.write("Some words FAILED, type d to check before power cycling\n"); }
        }
        else { serial.write(helpText); }
    }
}

} // namespace app::test_app
