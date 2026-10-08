/**
 * @file fordLogic.cpp
 * @brief Ford's logic: one loop, one drive style at a time. See ADR 0009.
 *
 * The loop is a fixed pipeline - read, decide, execute, publish - and `decideAction()`
 * dispatches to the selected drive style. A style fills in a PlannedDrive and nothing more;
 * the arm gate, the direction-change brake and the actuator-fault latch all sit in
 * `executeAction()`, below every style, so no style can reach past them.
 */
#include "system/logic/fordLogic.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

#include "driver/factory/interface.h"
#include "driver/i2c/esp32s3.h"
#include "driver/nvs/interface.h"
#include "driver/odometer/gaps.h"
#include "driver/odometer/store.h"
#include "driver/serial/esp32s3.h"
#include "driver/wifi/store.h"
#include "system/communication/pi_link.h"
#include "system/ford.h"
#include "system/runtime/names.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

namespace
{
// Ford's pins, geometry and settings come from system/ford.h, which the A89301
// configuration app shares so that both run the gap calibration on one wheel.
namespace ford = app::ford;

/** Ford's design gap fractions as a GapTable; see ford::DesignGapFractions. */
driver::odometer::GapTable fordDesignGaps() noexcept
{
    driver::odometer::GapTable table{};
    table.count = ford::OdometerMagnets;

    for (std::uint8_t index{0U}; index < ford::OdometerMagnets; ++index)
    {
        table.fraction[index] = ford::DesignGapFractions[index];
    }

    return table;
}

/** Write values as "a,b,c" with four decimals, for the CAL log lines. Truncates if full. */
void formatValues(const float* values, const std::uint8_t count, char* out, const std::size_t size) noexcept
{
    std::size_t used{0U};
    out[0] = '\0';
    for (std::uint8_t index{0U}; (index < count) && (used < size); ++index)
    {
        const int written = std::snprintf(out + used, size - used, index == 0U ? "%.4f" : ",%.4f",
                                          static_cast<double>(values[index]));
        if (written < 0) { break; }
        used += static_cast<std::size_t>(written);
    }
}

/** Name for the speed source, so the operator can see which reading they are looking at. */
const char* speedSourceName(driver::odometer::SpeedSource source) noexcept
{
    switch (source)
    {
        case driver::odometer::SpeedSource::None: return "none";
        case driver::odometer::SpeedSource::Revolution: return "revolution";
        case driver::odometer::SpeedSource::PerGap: return "per_gap";
    }
    return "none";
}

// Publication and command channels shared with the operator console.
// config/set is subscribed, but the Manager narrows it to the drive style for a
// remote-driven car: every other Ford setting stays compiled in, and a payload offering
// one is rejected rather than applied. See ADR 0009.
constexpr app::communication::Topics Topics{
    {"cnb/ford/telemetry", "cnb/ford/config/state", "cnb/ford/command/state", "cnb/ford/status"},
    {"cnb/ford/config/set", "cnb/ford/command"}};

// Car network: the operator laptop's hotspot, so the broker is always at 192.168.137.1.
// Compiled in, passwords included. The Wi-Fi network can be replaced over serial with the
// `wifi` command, which stores it in NVS; the broker stays compiled in.
constexpr app::communication::NetworkSettings Network{
    "cnb-net", "cnbrules",
    "mqtt://192.168.137.1:1883", "cnb-ford", "cnb-ford", "cnb"};

/** The UART to the Pi; see FordLogic::connectPiLink(). */
constexpr uart_port_t PiUartPort{UART_NUM_1};

/**
 * @brief Patterns the serial driver can queue; Esp32s3's QueueDepth, which it does not export.
 *
 * Passed again when the pattern queue is reset after a flush.
 */
constexpr int PiPatternQueueDepth{10};

/** USB serial console speed. The USB-JTAG link ignores it, but the factory asks for one. */
constexpr std::uint32_t SerialBaudRate{115200U};

constexpr const char* SerialHelpText{
    "Serial commands:\n"
    "  wifi              show the network in use and any unsaved changes\n"
    "  wifi ssid <name>  set the network name (spaces allowed)\n"
    "  wifi pass <pass>  set the password; leave it out for an open network\n"
    "  wifi save         store the new network; it is used after a restart\n"
    "  wifi clear        forget the stored network, back to the built-in one\n"
    "  servo <us>        send a raw steering pulse, 500-2400 us; disarmed only\n"
    "  servo             centre the steering again\n"
    "  help              this text\n"};

/**
 * @brief Raw pulses the `servo` command accepts: the MG90S's own full travel.
 *
 * Wider than the steering can move. Step in small increments and back off as soon as the
 * wheels stop or the servo hums, or it stalls against the end stop.
 */
constexpr long ServoCommandMinPulseUs{500};
constexpr long ServoCommandMaxPulseUs{2400};

/**
 * @brief Match a command word at the start of a line, ignoring case.
 *
 * @param[in] line Line to test.
 * @param[in] word Lower-case command word.
 * @return The text after the word with leading spaces skipped, or nullptr if the line does
 *         not start with the whole word.
 */
const char* afterWord(const char* line, const char* word) noexcept
{
    while (*word != '\0')
    {
        if (std::tolower(static_cast<unsigned char>(*line)) != *word) { return nullptr; }
        ++line;
        ++word;
    }
    if ((*line != '\0') && (*line != ' ')) { return nullptr; }
    while (*line == ' ') { ++line; }
    return line;
}

/**
 * @brief Copy a value typed over serial, refusing one that does not fit.
 *
 * @return True if copied, false if too long.
 */
template <std::size_t Size>
bool copyValue(char (&dst)[Size], const char* src) noexcept
{
    const std::size_t length{std::strlen(src)};
    if (length >= Size) { return false; }
    std::memcpy(dst, src, length + 1U);
    return true;
}

/**
 * @brief Turn a speed command into motor duty, skipping the duty band where the motor does not start.
 *
 * Commands below 1 go below ford::StartDuty in proportion, 0.5 to half of it. Nothing
 * drives there in normal use - the page's slider moves in whole steps - but it lets a
 * creep test on the floor find where a car that is already rolling finally stalls.
 * Measured 2026-10-07: down to command 1.2 the loaded car held about 0.59 m/s.
 *
 * @param speedCommand -100 full reverse, 0 no drive, +100 full forward.
 * @return 0 for no drive; below command 1, a share of ford::StartDuty; otherwise
 *         ford::StartDuty to ford::TopSpeedDuty.
 * @todo Add host tests for the mapping and its clamping.
 */
float dutyFor(float speedCommand) noexcept
{
    const float magnitude = std::min(std::abs(speedCommand), 100.0F);
    if (magnitude <= 0.0F) { return 0.0F; }
    if (magnitude < 1.0F) { return ford::StartDuty * magnitude; }
    return ford::StartDuty + (ford::TopSpeedDuty - ford::StartDuty) * (std::max(magnitude, 1.0F) - 1.0F) / 99.0F;
}
} // namespace

namespace app::logic
{

const char* FordLogic::toString(const MotorState state) noexcept
{
    switch (state)
    {
        case MotorState::Braked: return "braked";
        case MotorState::NoDrive: return "no_drive";
        case MotorState::Braking: return "braking";
        case MotorState::DrivingForward: return "driving_forward";
        case MotorState::DrivingReverse: return "driving_reverse";
    }
    return "braked";
}

FordLogic::FordLogic(driver::factory::Interface& factory) noexcept
    : myFactory{factory}
{
    myPreviousState = myControl.controlState();
    myPreviousMotion = myControl.motionState();
    myPreviousReason = myControl.stateReason();
}

FordLogic::~FordLogic() noexcept = default;

bool FordLogic::initializeDrivers() noexcept
{
    // Brake before anything else, so the wheels cannot spin while Wi-Fi and MQTT start.
    // If start-up fails, the pin is released and the BRAKE pull-up keeps the brake on.
    myBrake = myFactory.gpioOutput(ford::pin::Brake);
    if (!myBrake || !myBrake->isInitialized()) { ESP_LOGE("FORD", "Brake GPIO failed"); return false; }
    myBrake->write(true);
    myDirection = myFactory.gpioOutput(ford::pin::Direction);
    mySpeedPwm = myFactory.pwm(ford::pin::Speed);
    driver::pwm::Config steeringConfig{};
    steeringConfig.pin = ford::pin::Steering;
    steeringConfig.frequencyHz = ford::SteeringPwmFrequencyHz;
    mySteeringPwm = myFactory.pwm(steeringConfig);
    if (!myDirection || !myDirection->isInitialized() || !mySpeedPwm || !mySteeringPwm)
    { ESP_LOGE("FORD", "Driver allocation failed"); return false; }
    // Wrappers use the drivers above; member declaration order keeps them alive.
    myMotor = myFactory.fordMotor(*mySpeedPwm, *myDirection, *myBrake, ford::InvertMotorDirection);
    mySteering = myFactory.fordServo(*mySteeringPwm);
    // A89301::init() releases BRAKE with SPD at 0, so brake again straight after it.
    if (!myMotor || !mySteering || !myMotor->init() || !myMotor->stop(driver::motor::StopMode::Brake)
        || !mySteering->init())
    { myBrake->write(true); ESP_LOGE("FORD", "Initialization failed; brake held"); return false; }
    // The battery meter is optional: the car drives without it, and telemetry leaves it out.
    myBatteryAdc = myFactory.adc(ford::pin::BatteryAdc);
    if (myBatteryAdc)
    { myBattery = myFactory.voltageMeter(*myBatteryAdc, ford::BatteryR1Ohm, ford::BatteryR2Ohm); }
    if (!myBattery || !myBatteryAdc->init() || !myBattery->isInitialized())
    { ESP_LOGW("FORD", "Battery meter failed; battery voltage not reported"); }
    // The odometer is optional too: without it the car still drives to a speed command,
    // it just cannot report measured speed. The speed loop of ADR 0006 will need it.
    myOdometerGpio = myFactory.gpioInputPullup(ford::pin::Odometer);
    if (myOdometerGpio && myOdometerGpio->isInitialized())
    {
        myOdometer = myFactory.odometer(*myOdometerGpio, driver::odometer::Config{
            .pulsesPerRevolution = ford::OdometerMagnets,
            .wheelDiameterM = static_cast<float>(ford::WheelDiameterM),
        });
    }
    if (!myOdometer || !myOdometer->init())
    {
        myOdometer = nullptr; // Not unique_ptr::reset(): Interface has a reset() of its own.
        ESP_LOGW("FORD", "Odometer failed; measured speed and distance not reported");
    }
    loadOdometerGaps();
    // The motor temperature sensor is optional too; it is reported, not acted on.
    myMotorTempAdc = myFactory.adc(ford::pin::MotorTempAdc);
    if (myMotorTempAdc) { myMotorTemp = myFactory.temperatureSensor(*myMotorTempAdc); }
    if (!myMotorTemp || !myMotorTempAdc->init() || !myMotorTemp->isInitialized())
    { ESP_LOGW("FORD", "Motor temperature sensor failed; motor temperature not reported"); }
    // The steering servo temperature sensor is optional and reported only, like the motor's.
    myServoTempAdc = myFactory.adc(ford::pin::ServoTempAdc);
    if (myServoTempAdc) { myServoTemp = myFactory.temperatureSensor(*myServoTempAdc); }
    if (!myServoTemp || !myServoTempAdc->init() || !myServoTemp->isInitialized())
    { ESP_LOGW("FORD", "Servo temperature sensor failed; servo temperature not reported"); }

    // The A89301's fault output is optional and only reported. The board pulls it up to
    // IOREF, so a plain input: an internal pull-up would only fight a pin that is never
    // left floating.
    myMotorFaultGpio = myFactory.gpioInput(ford::pin::MotorFault);
    if (!myMotorFaultGpio || !myMotorFaultGpio->isInitialized())
    {
        myMotorFaultGpio = nullptr;
        ESP_LOGW("FORD", "A89301 FLT input failed; motor faults not reported");
    }

    connectDistanceSensors();

    // The serial console is optional too; without it the car joins whatever network it has.
    mySerial = myFactory.serial(SerialBaudRate);
    if (!mySerial || !mySerial->connect())
    {
        mySerial = nullptr;
        ESP_LOGW("FORD", "Serial console failed; the Wi-Fi network cannot be changed over serial");
    }
    loadWifiNetwork();
    connectPiLink();

    // Only now that the brake is on: the Manager's constructor allocates the Wi-Fi and
    // MQTT drivers, and the wheels must not be able to spin while those start.
    myCommunication = std::make_unique<app::communication::Manager>(myFactory, Topics, myNetwork);
    return myCommunication != nullptr;
}

void FordLogic::connectPiLink() noexcept
{
    // UART1, not UART0: the boot ROM and the log print on UART0, and the Pi must not
    // receive either. The receive buffer must be larger than the 128-byte hardware FIFO.
    myPiSerial = std::make_unique<driver::serial::Esp32s3>(driver::serial::Config{
        .port = PiUartPort,
        .txPin = ford::pin::PiUartTx,
        .rxPin = ford::pin::PiUartRx,
        .baudRate = static_cast<int>(ford::PiUartBaudRate),
        .rxBufSize = 1024U,
    });
    if (!myPiSerial->connect())
    {
        myPiSerial = nullptr;
        ESP_LOGW("FORD", "Pi UART failed; the car is driven over MQTT only");
        return;
    }
    // A powered-off Pi leaves RX floating; the pull-up makes that read as an idle line, not noise.
    gpio_pullup_en(static_cast<gpio_num_t>(ford::pin::PiUartRx));
    ESP_LOGI("FORD", "Pi link: UART1 at %lu baud", static_cast<unsigned long>(ford::PiUartBaudRate));
}

void FordLogic::receivePiLines(const std::uint32_t nowMs) noexcept
{
    if (!myPiSerial) { return; }

    // Bounded, so a flood on the wire cannot hold up the loop. The Pi sends one line per
    // pi_link::PeriodMs, and the loop runs far faster than that.
    constexpr int MaxLinesPerTick{8};
    for (int count{0}; (count < MaxLinesPerTick) && myPiSerial->isDataAvailable(); ++count)
    {
        char line[app::pi_link::MaxLineLength + 2U]{};
        if (myPiSerial->read(line, sizeof(line)) == 0U) { break; }

        app::pi_link::PiLine parsed{};
        if (!app::pi_link::parsePiLine(line, parsed))
        {
            ++myPiLink.dropped;
            continue;
        }
        myPiLink.lastReceivedSequence = parsed.sequence;
        (void)myControl.handlePiLine(parsed.state, parsed.steeringCommand, parsed.speedCommand, nowMs);
    }

    // Lines are only ever taken out at a newline. A Pi that is off or booting holds the
    // line low, which arrives as bytes with no newline in them; they fill the receive
    // buffer, and once it is full every later byte - the Pi's good lines too - is thrown
    // away, for good. Seen on 2026-10-07 when the Pi's power was switched: the link stayed
    // "gone" until the ESP was reset. So bytes with no line in them are flushed.
    std::size_t buffered{0U};
    if (!myPiSerial->isDataAvailable()
        && (uart_get_buffered_data_len(PiUartPort, &buffered) == ESP_OK)
        && (buffered > 2U * app::pi_link::MaxLineLength))
    {
        uart_flush_input(PiUartPort);
        uart_pattern_queue_reset(PiUartPort, PiPatternQueueDepth);
        // Counted, not logged: a Pi that stays off would refill it on every tick.
        ++myPiLink.dropped;
    }
}

void FordLogic::sendCarLine(const std::uint32_t nowMs) noexcept
{
    if (!myPiSerial || ((nowMs - myPiLink.lastSentMs) < app::pi_link::PeriodMs)) { return; }
    myPiLink.lastSentMs = nowMs;

    app::pi_link::CarLine line{};
    line.sequence = ++myPiLink.sequence;
    line.controlState = app::runtime::toString(myControl.controlState());
    line.reason = app::runtime::toString(myControl.stateReason());
    line.driveStyle = app::navigation::toString(myControl.configuration().driveStyle);
    line.piLink = app::runtime::toString(myControl.piLink(nowMs));
    // What publishState() just worked out the car applies, not what anyone asked for.
    line.appliedSteering = mySnapshot.steeringDegrees;
    line.appliedSpeed = mySnapshot.speedCommand;
    line.odometerDistanceM = myOdometer ? myOdometer->distance() : std::numeric_limits<float>::quiet_NaN();
    line.odometerSpeedMs = myOdometer ? myOdometer->speed() : std::numeric_limits<float>::quiet_NaN();
    line.odometerSpeedSource = myOdometer ? speedSourceName(myOdometer->speedSource()) : "";
    line.lastPiSequence = myPiLink.lastReceivedSequence;
    // What turnDistanceSensors() stored this tick: NaN, an empty field, without a reading.
    line.distanceForwardCm = myDistanceForward ? myDistanceForward->readDistance()
                                               : std::numeric_limits<float>::quiet_NaN();
    line.distanceLeftCm = myDistanceLeft ? myDistanceLeft->readDistance()
                                         : std::numeric_limits<float>::quiet_NaN();
    line.distanceRightCm = myDistanceRight ? myDistanceRight->readDistance()
                                           : std::numeric_limits<float>::quiet_NaN();

    char out[app::pi_link::MaxLineLength + 8U]{};
    if (app::pi_link::formatCarLine(line, out, sizeof(out)) > 0U) { myPiSerial->write(out); }
}

void FordLogic::loadOdometerGaps() noexcept
{
    if (!myOdometer) { return; }

    // A measured table if one has been stored, the design values otherwise. Scoped so
    // the namespace is released again: the car only reads it, and the calibration
    // session needs to be able to open it.
    auto gaps = fordDesignGaps();
    const char* gapSource{"design values"};
    {
        const driver::odometer::Store store{myFactory.nvs()};
        driver::odometer::GapTable stored{};
        float spread{0.0F};
        if (store.load(ford::OdometerMagnets, stored, spread))
        {
            gaps = stored;
            gapSource = "calibration";
            ESP_LOGI("FORD", "Odometer gaps from calibration, spread %.4f", spread);
        }
    }
    if (!myOdometer->setGapTable(gaps))
    {
        ESP_LOGW("FORD", "Odometer gap table refused; speed stays on the revolution window");
    }
    else
    {
        ESP_LOGI("FORD", "Odometer: %u magnets, gaps from %s", ford::OdometerMagnets, gapSource);
    }
}

void FordLogic::loadWifiNetwork() noexcept
{
    myNetwork = Network;
    myWifiFromStore = false;
    {
        // Scoped so the namespace is released again for `wifi save` and `wifi clear`.
        const driver::wifi::Store store{myFactory.nvs()};
        if (store.load(myWifiSsid, myWifiPassword))
        {
            myNetwork.wifiSsid = myWifiSsid;
            myNetwork.wifiPassword = myWifiPassword;
            myWifiFromStore = true;
        }
    }
    ESP_LOGI("FORD", "Wi-Fi: joining \"%s\" (%s)", myNetwork.wifiSsid,
             myWifiFromStore ? "set over serial" : "built in");
}

void FordLogic::processSerialCommand() noexcept
{
    if (!mySerial || !mySerial->isDataAvailable()) { return; }

    char line[128]{};
    if (mySerial->read(line, sizeof(line)) == 0U) { return; }

    // Trailing spaces would otherwise end up in an SSID or password, invisibly.
    std::size_t length{std::strlen(line)};
    while ((length > 0U) && (line[length - 1U] == ' ')) { line[--length] = '\0'; }

    const char* start{line};
    while (*start == ' ') { ++start; }

    if (const char* args{afterWord(start, "wifi")}) { handleWifiCommand(args); }
    else if (const char* args{afterWord(start, "servo")}) { handleServoCommand(args); }
    else if (afterWord(start, "help") != nullptr) { mySerial->write(SerialHelpText); }
    else if (*start != '\0') { mySerial->write("Unknown command. Type help.\n"); }
}

void FordLogic::handleServoCommand(const char* args) noexcept
{
    if (myControl.controlState() != app::runtime::ControlState::Disarmed)
    {
        mySerial->write("Stop the car first: servo works only while disarmed.\n");
        return;
    }
    if (*args == '\0')
    {
        // executeAction() writes the steering again when it differs from what was applied.
        myAppliedSteering = std::numeric_limits<float>::quiet_NaN();
        myRawSteeringPulse = false;
        mySerial->write("Steering centred.\n");
        return;
    }

    char* end{nullptr};
    const long pulseUs{std::strtol(args, &end, 10)};
    if ((end == args) || (*end != '\0') || (pulseUs < ServoCommandMinPulseUs)
        || (pulseUs > ServoCommandMaxPulseUs))
    {
        mySerial->write("Usage: servo <us>, a whole number from 500 to 2400, or servo to centre.\n");
        return;
    }

    // Straight to the PWM, past the servo driver's left/centre/right mapping being measured.
    // The applied steering is left alone, so executeAction() holds this pulse while disarmed.
    // Start drops it; see executeAction().
    const float duty{static_cast<float>(pulseUs) * static_cast<float>(mySteeringPwm->frequencyHz())
                     / 1'000'000.0F};
    char out[96]{};
    if (!mySteeringPwm->setDuty(duty))
    {
        mySerial->write("Could not set the steering pulse.\n");
        return;
    }
    myRawSteeringPulse = true;
    std::snprintf(out, sizeof(out), "Steering pulse %ld us. Back off if the servo hums.\n", pulseUs);
    mySerial->write(out);
}

void FordLogic::handleWifiCommand(const char* args) noexcept
{
    char out[160]{};

    if (*args == '\0')
    {
        std::snprintf(out, sizeof(out), "Network in use: \"%s\" (%s), %s\n", myNetwork.wifiSsid,
                      myWifiFromStore ? "set over serial" : "built in",
                      ((myNetwork.wifiPassword != nullptr) && (*myNetwork.wifiPassword != '\0'))
                          ? "password set" : "open network");
        mySerial->write(out);
        if (myPendingWifi.hasSsid || myPendingWifi.hasPassword)
        {
            std::snprintf(out, sizeof(out), "Unsaved: name \"%s\", password %s. Type wifi save.\n",
                          myPendingWifi.hasSsid ? myPendingWifi.ssid : "(not set)",
                          !myPendingWifi.hasPassword ? "(not set)"
                              : ((*myPendingWifi.password == '\0') ? "none (open)" : "set"));
            mySerial->write(out);
        }
        return;
    }

    if (const char* value{afterWord(args, "ssid")})
    {
        if ((*value == '\0') || !copyValue(myPendingWifi.ssid, value))
        {
            mySerial->write("The network name must be 1 to 32 characters.\n");
            return;
        }
        myPendingWifi.hasSsid = true;
        std::snprintf(out, sizeof(out), "Name set to \"%s\". Now wifi pass <password>, then wifi save.\n",
                      myPendingWifi.ssid);
        mySerial->write(out);
        return;
    }

    if (const char* value{afterWord(args, "pass")})
    {
        const std::size_t length{std::strlen(value)};
        if (((length > 0U) && (length < 8U)) || !copyValue(myPendingWifi.password, value))
        {
            mySerial->write("The password must be 8 to 63 characters, or left out for an open network.\n");
            return;
        }
        myPendingWifi.hasPassword = true;
        mySerial->write((length == 0U) ? "Open network, no password. Now wifi save.\n"
                                       : "Password set. Now wifi save.\n");
        return;
    }

    if (afterWord(args, "save") != nullptr)
    {
        if (!myPendingWifi.hasSsid || !myPendingWifi.hasPassword)
        {
            mySerial->write("Set both first: wifi ssid <name> and wifi pass <password>.\n");
            return;
        }
        bool saved{false};
        {
            driver::wifi::Store store{myFactory.nvs()};
            saved = store.save(myPendingWifi.ssid, myPendingWifi.password);
        }
        if (!saved)
        {
            mySerial->write("Saving failed. Nothing was changed.\n");
            ESP_LOGE("FORD", "Wi-Fi store write failed");
            return;
        }
        std::snprintf(out, sizeof(out), "Saved \"%s\". Press reset to join it.\n", myPendingWifi.ssid);
        mySerial->write(out);
        ESP_LOGI("FORD", "Wi-Fi: \"%s\" stored; used after the next restart", myPendingWifi.ssid);
        myPendingWifi = PendingWifi{};
        return;
    }

    if (afterWord(args, "clear") != nullptr)
    {
        bool cleared{false};
        {
            driver::wifi::Store store{myFactory.nvs()};
            cleared = store.clear();
        }
        myPendingWifi = PendingWifi{};
        if (cleared)
        {
            std::snprintf(out, sizeof(out), "Stored network forgotten. After a restart: \"%s\".\n",
                          Network.wifiSsid);
            mySerial->write(out);
        }
        else { mySerial->write("Clearing failed.\n"); }
        return;
    }

    mySerial->write("Unknown wifi command. Type help.\n");
}

void FordLogic::readSensors(const std::uint32_t nowMs) noexcept
{
    if (myBattery && (!myBatteryRead || (nowMs - myLastBatteryReadMs) >= ford::BatteryReadIntervalMs))
    {
        mySnapshot.batteryVoltage = myBattery->readVoltage();
        myLastBatteryReadMs = nowMs;
        myBatteryRead = true;
    }
    if (myMotorTemp
        && (!myMotorTempRead || (nowMs - myLastMotorTempReadMs) >= ford::MotorTempReadIntervalMs))
    {
        myMotorTemperatureC = myMotorTemp->readTemperature();
        mySnapshot.motorTemperatureC = myMotorTemperatureC;
        myLastMotorTempReadMs = nowMs;
        myMotorTempRead = true;
    }
    // FLT flashes while a fault lasts, so it is read every tick and a low is held for
    // MotorFaultHoldMs: one reading of the level would miss half of every fault.
    if (myMotorFaultGpio)
    {
        if (!myMotorFaultGpio->read())
        {
            if (!myMotorFault)
            {
                ++myMotorFaultCount;
                ESP_LOGW("FORD", "A89301 reports a fault (FLT low), %lu since start-up",
                         static_cast<unsigned long>(myMotorFaultCount));
            }
            myMotorFault = true;
            myLastMotorFaultMs = nowMs;
        }
        else if (myMotorFault && (nowMs - myLastMotorFaultMs) >= ford::MotorFaultHoldMs)
        {
            myMotorFault = false;
        }
        mySnapshot.motorFault = myMotorFault ? 1 : 0;
        mySnapshot.motorFaultCount = myMotorFaultCount;
    }
    if (myServoTemp
        && (!myServoTempRead || (nowMs - myLastServoTempReadMs) >= ford::ServoTempReadIntervalMs))
    {
        mySnapshot.servoTemperatureC = myServoTemp->readTemperature();
        myLastServoTempReadMs = nowMs;
        myServoTempRead = true;
    }
    turnDistanceSensors(nowMs);
}

void FordLogic::connectDistanceSensors() noexcept
{
    myDistanceBus = std::make_unique<driver::i2c::Esp32s3>(driver::i2c::Config{
        ford::pin::Sda, ford::pin::Scl, driver::i2c::DefaultFrequencyHz, true});
    if (!myDistanceBus || !myDistanceBus->init())
    {
        myDistanceBus = nullptr;
        ESP_LOGW("FORD", "Qwiic bus did not start; no distance sensors");
        return;
    }

    const auto connect{[this](const std::uint8_t address, const char* const what)
                       -> std::unique_ptr<driver::distance_sensor::HcSr04Qwiic> {
        auto sensor{std::make_unique<driver::distance_sensor::HcSr04Qwiic>(*myDistanceBus, address)};
        if (!sensor || !sensor->isInitialized())
        {
            ESP_LOGW("FORD", "No HC-SR04 answered 0x%02X; %s distance not reported",
                     static_cast<unsigned>(address), what);
            return nullptr;
        }
        return sensor;
    }};

    myDistanceForward = connect(ford::distance_sensor::ForwardAddress, "forward");
    myDistanceLeft    = connect(ford::distance_sensor::LeftAddress, "left");
    myDistanceRight   = connect(ford::distance_sensor::RightAddress, "right");

    // One before the first, so the forward sensor is the one that gets the first ping.
    myDistanceTurn = static_cast<std::uint8_t>(ford::distance_sensor::Count - 1U);
}

void FordLogic::turnDistanceSensors(const std::uint32_t nowMs) noexcept
{
    // Slots of TelemetrySnapshot::distancesCm, which the serializer names left/center/right.
    constexpr std::size_t Left{0U};
    constexpr std::size_t Center{1U};
    constexpr std::size_t Right{2U};

    if (!myDistanceBus) { return; }

    driver::distance_sensor::HcSr04Qwiic* const sensors[ford::distance_sensor::Count]{
        myDistanceForward.get(), myDistanceLeft.get(), myDistanceRight.get()};

    // Collect from whichever sensor holds the ping. poll() returns at once until its result
    // is due, and drops a reading the car has outlived even when the sensor has gone quiet.
    for (auto* const sensor : sensors)
    {
        if (sensor != nullptr) { (void)sensor->poll(nowMs); }
    }

    const bool turnDue{!myDistanceStarted
                       || ((nowMs - myLastDistanceTurnMs) >= ford::distance_sensor::TurnIntervalMs)};
    if (turnDue)
    {
        // Hand the ping on, stepping over sensors that are not fitted or did not answer. If
        // none of them takes it the ring stops turning, which is what no sensors should do.
        for (std::uint8_t step{0U}; step < ford::distance_sensor::Count; ++step)
        {
            myDistanceTurn =
                static_cast<std::uint8_t>((myDistanceTurn + 1U) % ford::distance_sensor::Count);
            auto* const next{sensors[myDistanceTurn]};
            if ((next != nullptr) && next->trigger(nowMs))
            {
                myLastDistanceTurnMs = nowMs;
                myDistanceStarted    = true;
                break;
            }
        }
    }

    // distance_cm in telemetry is {left, center, right} and has been since Vagrant's IR
    // sensors, so Ford's forward sensor is reported as "center" rather than inventing a
    // second shape. The serializer turns a NaN into a null and works out "closest" itself.
    constexpr float None{std::numeric_limits<float>::quiet_NaN()};
    mySnapshot.distancesCm[Left]    = myDistanceLeft ? myDistanceLeft->readDistance() : None;
    mySnapshot.distancesCm[Center]  = myDistanceForward ? myDistanceForward->readDistance() : None;
    mySnapshot.distancesCm[Right]   = myDistanceRight ? myDistanceRight->readDistance() : None;
}

void FordLogic::checkSafeMode() noexcept
{
    if (!ford::SafeModeEnabled) { return; }
    // Disabled keeps the cause it was entered with until the operator selects another style.
    if (myControl.configuration().driveStyle == navigation::DriveStyle::Disabled) { return; }
    mySnapshot.disabledCause = nullptr;

    const char* cause{nullptr};
    float temperatureC{std::numeric_limits<float>::quiet_NaN()};
    // A missing sensor reads NaN, and NaN never compares as hot.
    if (myMotorTemperatureC >= ford::SafeModeMaxTempC)
    { cause = "motor_temp"; temperatureC = myMotorTemperatureC; }
    else if (mySnapshot.servoTemperatureC >= ford::SafeModeMaxTempC)
    { cause = "servo_temp"; temperatureC = mySnapshot.servoTemperatureC; }
    if (cause == nullptr) { return; }

    // A style only changes while disarmed. executeAction() brakes a disarmed car.
    if (myControl.controlState() != app::runtime::ControlState::Disarmed)
    { myControl.forceDisarm(app::runtime::StateReason::Overheated); }
    if (!myControl.setDriveStyle(navigation::DriveStyle::Disabled)) { return; }
    mySnapshot.disabledCause = cause;
    mySnapshot.disabledTemperatureC = temperatureC;
    ESP_LOGW("FORD", "Safe mode: %s %.1f C reached %.1f C; drive style Disabled", cause,
             static_cast<double>(temperatureC), static_cast<double>(ford::SafeModeMaxTempC));
}

void FordLogic::decideAction(const std::uint32_t nowMs) noexcept
{
    switch (myControl.configuration().driveStyle)
    {
        case navigation::DriveStyle::ManualByRemote:
            decideManualByRemoteAction(nowMs);
            break;
        case navigation::DriveStyle::GapCalibration:
            decideGapCalibrationAction(nowMs);
            break;
        case navigation::DriveStyle::SpeedCalibration:
            decideSpeedCalibrationAction(nowMs);
            break;
        // Safe mode parked the car. A Start is undone at once, so the page shows why.
        case navigation::DriveStyle::Disabled:
            if (myControl.controlState() != app::runtime::ControlState::Disarmed)
            { myControl.forceDisarm(app::runtime::StateReason::Overheated); }
            myPlannedDrive = {};
            break;
        // A remote-driven car never has an autonomous style selected: Control's
        // supportsDriveStyle() refuses them, so these cannot be reached. No drive is
        // nevertheless the right answer rather than carrying on with a stale request.
        case navigation::DriveStyle::DecideAction:
        case navigation::DriveStyle::SlowLeft:
        case navigation::DriveStyle::SlowRight:
        case navigation::DriveStyle::GradualSweep:
            myPlannedDrive = {};
            break;
    }
}

void FordLogic::decideManualByRemoteAction(const std::uint32_t nowMs) noexcept
{
    // 0/0 while disarmed; speed 0 after a drive timeout. remoteDrive() also enforces the
    // operator's lease for this style.
    const auto drive = myControl.remoteDrive(nowMs, ford::DriveTimeoutMs);
    myManualByRemote.lastDrive = drive;

    // The operator asks in speed commands; every style asks the car in duty. dutyFor()
    // skips the band where the motor will not start, so this mapping is the style's job.
    const float magnitude = dutyFor(drive.speedCommand);
    myPlannedDrive.duty = drive.speedCommand < 0.0F ? -magnitude : magnitude;
    myPlannedDrive.steeringCommand = drive.steeringCommand;
}

void FordLogic::decideGapCalibrationAction(const std::uint32_t nowMs) noexcept
{
    // The measurement never steers: the wheel it measures is the right rear one, and the
    // car is on a stand. Centre the steering and ask for nothing until the run says so.
    myPlannedDrive = {};

    // The lease is style-independent. remoteDrive() enforces it for ManualByRemote, and
    // this style never calls remoteDrive, so without this a console that went quiet would
    // leave a powered wheel turning for the rest of the run.
    const bool armed = myControl.renewLease(nowMs);

    // Arming starts a run; the edge, not the level, because a finished run disarms.
    if (armed && !myCalibration.wasArmed)
    {
        myCalibration.run.start(nowMs, ford::OdometerMagnets);
        myCalibration.storeFailed = false;
        myCalibration.loggedSerial = 0U;
        myCalibration.loggedDuties = 0U;
        ESP_LOGI("FORD", "GapCalibration: starting, %u magnets", ford::OdometerMagnets);
        char duties[64];
        formatValues(ford::calibration::Duties, ford::calibration::DutyCount, duties, sizeof(duties));
        ESP_LOGI("CAL", "start magnets=%u duties=%s settle_ms=%lu revolutions=%u",
                 ford::OdometerMagnets, duties,
                 static_cast<unsigned long>(ford::calibration::SettleMs),
                 ford::calibration::Revolutions);
    }
    myCalibration.wasArmed = armed;

    if (!myCalibration.run.isRunning()) { return; }

    myPlannedDrive.duty =
        myCalibration.run.update(nowMs, armed, myMotorTemperatureC, myOdometer.get());
    logCalibration();

    // A run that has just ended disarms the car, so Start triggers the next one. Finishing
    // is not a fault, so it does not go through the fail-safe disarm. A run ended by a
    // disarm (a stop, a lost connection, a stale heartbeat) is already disarmed with the
    // real reason, and disarming again would overwrite it with "finished".
    if (!myCalibration.run.isRunning())
    {
        myPlannedDrive.duty = 0.0F;
        if (armed) { myControl.finishDriveStyle(); }
        myCalibration.wasArmed = false;
        if (myCalibration.run.hasTable())
        {
            ESP_LOGI("FORD", "GapCalibration: measured, spread %.4f, margin %.4f; awaiting confirm",
                     myCalibration.run.result().spread, myCalibration.run.result().margin);
        }
        else
        {
            ESP_LOGW("FORD", "GapCalibration: no table stored; failure %u",
                     static_cast<unsigned>(myCalibration.run.failure()));
        }
    }
}

void FordLogic::logCalibration() noexcept
{
    // Tag CAL, one line per event, so a serial capture can be filtered with grep and the
    // run analysed offline: every revolution, each duty's average, then the verdict.
    namespace odo = driver::odometer;
    const auto& run = myCalibration.run;
    char values[96];

    const auto& sample = run.lastSample();
    if (sample.serial != myCalibration.loggedSerial)
    {
        myCalibration.loggedSerial = sample.serial;
        formatValues(sample.gaps, ford::OdometerMagnets, values, sizeof(values));
        ESP_LOGI("CAL", "rev n=%lu duty=%u pulses=%lu ms=%lu gaps=%s",
                 static_cast<unsigned long>(sample.serial), sample.dutyIndex,
                 static_cast<unsigned long>(sample.pulseStep),
                 static_cast<unsigned long>(sample.intervalMs), values);
    }

    while ((myCalibration.loggedDuties < ford::calibration::DutyCount)
           && (run.measured(myCalibration.loggedDuties).count > 0U))
    {
        const auto& table = run.measured(myCalibration.loggedDuties);
        float margin{0.0F};
        (void)odo::bestRotation(table.fraction, table, margin);
        formatValues(table.fraction, table.count, values, sizeof(values));
        ESP_LOGI("CAL", "duty %u avg=%s margin=%.4f",
                 myCalibration.loggedDuties, values, static_cast<double>(margin));
        ++myCalibration.loggedDuties;
    }

    if (run.isRunning()) { return; }

    const auto& result = run.result();
    if (result.table.count > 0U)
    {
        formatValues(result.table.fraction, result.table.count, values, sizeof(values));
    }
    else
    {
        std::snprintf(values, sizeof(values), "none");
    }
    const char* failure = toString(run.failure());
    ESP_LOGI("CAL", "result phase=%s failure=%s mean=%s spread=%.4f worst_gap=%u margin=%.4f (need >= %.2f)",
             toString(run.phase()), (failure != nullptr) ? failure : "none", values,
             static_cast<double>(result.spread), result.worstGap,
             static_cast<double>(result.margin), static_cast<double>(odo::MinPhaseMargin));
}

void FordLogic::decideSpeedCalibrationAction(const std::uint32_t nowMs) noexcept
{
    // Straight ahead throughout: the operator lines the car up and stops it if it drifts.
    myPlannedDrive = {};
    auto& state = mySpeedCalibration;

    // The lease is style-independent; see decideGapCalibrationAction().
    const bool armed = myControl.renewLease(nowMs);

    // Arming starts a run; the edge, not the level, because a finished run disarms.
    if (armed && !state.wasArmed)
    {
        state.run.start(nowMs, ford::OdometerMagnets, static_cast<float>(ford::WheelCircumferenceM));
        state.loggedAny = false;
        char targets[64];
        formatValues(ford::speed_calibration::TargetsMs, ford::speed_calibration::TargetCount, targets,
                     sizeof(targets));
        ESP_LOGI("SPD", "start targets=%s legs=%u leg_m=%.2f stop_k=%.3f",
                 targets, ford::speed_calibration::LegCount,
                 static_cast<double>(ford::speed_calibration::LegM),
                 static_cast<double>(state.run.stopK()));
    }
    state.wasArmed = armed;

    if (!state.run.isRunning()) { return; }

    myPlannedDrive.duty = state.run.update(nowMs, armed, myMotorTemperatureC, myOdometer.get());
    myPlannedDrive.brake = state.run.isBraking();
    logSpeedCalibration();

    // As for GapCalibration: a finished run disarms with its own reason, a disarmed one
    // already has the real reason and is left alone.
    if (!state.run.isRunning())
    {
        myPlannedDrive = {};
        if (armed) { myControl.finishDriveStyle(); }
        state.wasArmed = false;
        const char* failure = toString(state.run.failure());
        ESP_LOGI("SPD", "end phase=%s failure=%s", toString(state.run.phase()),
                 (failure != nullptr) ? failure : "none");
    }
}

void FordLogic::logSpeedCalibration() noexcept
{
    // Tag SPD, one line per leg, so a serial capture can be filtered with grep.
    auto& state = mySpeedCalibration;
    if (!state.run.hasLastLeg()) { return; }

    const auto& leg = state.run.lastLeg();
    if (state.loggedAny && (state.loggedLeg == leg.index)) { return; }
    state.loggedAny = true;
    state.loggedLeg = leg.index;

    ESP_LOGI("SPD", "leg n=%u from=%.2f target=%.2f dir=%s result=%s speed_ms=%.3f duty=%.3f "
             "rise_s=%.2f overshoot=%.3f stop_m=%.3f distance_m=%.3f stop_k=%.3f",
             leg.index, static_cast<double>(leg.plan.fromMs), static_cast<double>(leg.plan.targetMs),
             leg.plan.forward ? "fwd" : "back", toString(leg.outcome),
             static_cast<double>(leg.speedMs), static_cast<double>(leg.duty),
             static_cast<double>(leg.riseS), static_cast<double>(leg.overshootMs),
             static_cast<double>(leg.stopM), static_cast<double>(leg.distanceM),
             static_cast<double>(state.run.stopK()));
}

const char* FordLogic::toString(const SpeedCalibration::Phase phase) noexcept
{
    switch (phase)
    {
        case SpeedCalibration::Phase::Idle: return "idle";
        case SpeedCalibration::Phase::Driving: return "driving";
        case SpeedCalibration::Phase::Braking: return "braking";
        case SpeedCalibration::Phase::Finished: return "finished";
        case SpeedCalibration::Phase::Failed: return "failed";
    }
    return "idle";
}

const char* FordLogic::toString(const SpeedCalibration::Failure failure) noexcept
{
    switch (failure)
    {
        case SpeedCalibration::Failure::None: return nullptr;
        case SpeedCalibration::Failure::NoOdometer: return "no_odometer";
        case SpeedCalibration::Failure::TooHot: return "too_hot";
        case SpeedCalibration::Failure::Stopped: return "stopped";
    }
    return nullptr;
}

const char* FordLogic::toString(const SpeedCalibration::Outcome outcome) noexcept
{
    switch (outcome)
    {
        case SpeedCalibration::Outcome::None: return "none";
        case SpeedCalibration::Outcome::Reached: return "reached";
        case SpeedCalibration::Outcome::Short: return "short";
        case SpeedCalibration::Outcome::NotReached: return "not_reached";
        case SpeedCalibration::Outcome::Lowest: return "lowest";
        case SpeedCalibration::Outcome::Bottom: return "bottom";
        case SpeedCalibration::Outcome::NoStart: return "no_start";
        case SpeedCalibration::Outcome::Stalled: return "stalled";
    }
    return "none";
}

void FordLogic::publishSpeedCalibrationState() noexcept
{
    auto& out = mySnapshot.speedCalibration;
    if (myControl.configuration().driveStyle != navigation::DriveStyle::SpeedCalibration)
    {
        out.phase = nullptr;
        return;
    }

    const auto& run = mySpeedCalibration.run;
    out.phase = toString(run.phase());
    out.failure = toString(run.failure());
    out.leg = run.legIndex();
    out.legCount = ford::speed_calibration::LegCount;
    out.legM = ford::speed_calibration::LegM;
    const auto plan = SpeedCalibration::planOf(run.legIndex());
    out.fromMs = plan.fromMs;
    out.targetMs = plan.targetMs;
    out.forward = plan.forward;
    out.stopK = run.stopK();

    if (!run.hasLastLeg())
    {
        out.lastResult = nullptr;
        return;
    }
    const auto& leg = run.lastLeg();
    out.lastResult = toString(leg.outcome);
    out.lastLeg = leg.index;
    out.lastFromMs = leg.plan.fromMs;
    out.lastTargetMs = leg.plan.targetMs;
    out.lastForward = leg.plan.forward;
    out.lastSpeedMs = leg.speedMs;
    out.lastDuty = leg.duty;
    out.lastRiseS = leg.riseS;
    out.lastOvershootMs = leg.overshootMs;
    out.lastStopM = leg.stopM;
    out.lastDistanceM = leg.distanceM;
}

const char* FordLogic::toString(const GapCalibration::Phase phase) noexcept
{
    switch (phase)
    {
        case GapCalibration::Phase::Idle: return "idle";
        case GapCalibration::Phase::Settling: return "settling";
        case GapCalibration::Phase::Sampling: return "sampling";
        case GapCalibration::Phase::Measured: return "measured";
        case GapCalibration::Phase::Failed: return "failed";
    }
    return "idle";
}

const char* FordLogic::toString(const GapCalibration::Failure failure) noexcept
{
    switch (failure)
    {
        case GapCalibration::Failure::None: return nullptr;
        case GapCalibration::Failure::NoOdometer: return "no_odometer";
        case GapCalibration::Failure::Stalled: return "stalled";
        case GapCalibration::Failure::TooHot: return "too_hot";
        case GapCalibration::Failure::Disagreed: return "disagreed";
        case GapCalibration::Failure::NotPlausible: return "not_plausible";
        case GapCalibration::Failure::ThinMargin: return "thin_phase_margin";
        case GapCalibration::Failure::Stopped: return "stopped";
    }
    return nullptr;
}

void FordLogic::publishCalibrationState() noexcept
{
    // Absent means "not this drive style", the same way every optional telemetry field
    // means "not available" by being missing rather than by being sent empty.
    if (myControl.configuration().driveStyle != navigation::DriveStyle::GapCalibration)
    {
        mySnapshot.calibrationPhase = nullptr;
        return;
    }

    const auto& run = myCalibration.run;
    mySnapshot.calibrationPhase = toString(run.phase());
    mySnapshot.calibrationFailure = toString(run.failure());
    mySnapshot.calibrationDutyIndex = run.dutyIndex();
    mySnapshot.calibrationDutyCount = ford::calibration::DutyCount;
    mySnapshot.calibrationSamples = run.samples();
    mySnapshot.calibrationRevolutions = ford::calibration::Revolutions;
    mySnapshot.calibrationStored = run.isStored();
    mySnapshot.calibrationStoreFailed = myCalibration.storeFailed;
    // The guard is off when the sensor is missing or failed, and the operator is told.
    mySnapshot.calibrationOverheatGuard = std::isfinite(myMotorTemperatureC);

    if (run.hasTable())
    {
        mySnapshot.calibrationGaps = run.result().table.fraction;
        mySnapshot.calibrationGapCount = run.result().table.count;
        mySnapshot.calibrationSpread = run.result().spread;
        mySnapshot.calibrationMargin = run.result().margin;
    }
    else
    {
        mySnapshot.calibrationGaps = nullptr;
        mySnapshot.calibrationGapCount = 0U;
        // A disagreement is worth reporting even though it produced no table: the spread
        // is the evidence, and it is what tells the operator to space the magnets better.
        const bool disagreed = run.failure() == GapCalibration::Failure::Disagreed;
        mySnapshot.calibrationSpread =
            disagreed ? run.result().spread : std::numeric_limits<float>::quiet_NaN();
        mySnapshot.calibrationMargin = std::numeric_limits<float>::quiet_NaN();
    }
}

void FordLogic::storeMeasuredGaps() noexcept
{
    if (!myCalibration.run.hasTable())
    {
        ESP_LOGW("FORD", "Store refused: no measured gap table is waiting");
        myCalibration.storeFailed = true;
        return;
    }

    // Scoped so the namespace is released again, as loadOdometerGaps() does: the store
    // owns the namespace for as long as it lives.
    bool saved{false};
    {
        driver::odometer::Store store{myFactory.nvs()};
        const auto& result = myCalibration.run.result();
        saved = store.isOpen() && store.save(result.table, result.spread);
    }
    myCalibration.storeFailed = !saved;
    if (!saved)
    {
        ESP_LOGE("FORD", "Store write failed; the measured table is still waiting");
        return;
    }

    myCalibration.run.markStored();
    ESP_LOGI("FORD", "GapCalibration: stored. It takes effect on the next restart.");
}

void FordLogic::executeAction(const std::uint32_t nowMs) noexcept
{
    const bool armed = myControl.controlState() == app::runtime::ControlState::Armed;

    MotorState wanted{MotorState::Braked};
    float duty{0.0F};
    const auto wantedDirection = myPlannedDrive.duty < 0.0F
        ? driver::motor::Direction::Backward
        : driver::motor::Direction::Forward;
    // An armed car steers by its commands, never by a `servo` measuring pulse.
    if (armed && myRawSteeringPulse)
    {
        myAppliedSteering = std::numeric_limits<float>::quiet_NaN();
        myRawSteeringPulse = false;
    }
    if (!armed || myActuatorFault) { myBraking = false; }
    else if (myPlannedDrive.duty == 0.0F)
    {
        wanted = myPlannedDrive.brake ? MotorState::Braking : MotorState::NoDrive;
        myBraking = false;
    }
    else
    {
        // Never reverse a spinning sensorless motor: brake first, then change DIR.
        const bool reversing = myHasDriven && wantedDirection != myDrivenDirection;
        if (!reversing) { myBraking = false; }
        else if (!myBraking) { myBraking = true; myBrakeStartMs = nowMs; }
        if (myBraking && (nowMs - myBrakeStartMs) < ford::DirectionChangeBrakeMs)
        { wanted = MotorState::Braking; }
        else
        {
            myBraking = false;
            wanted = wantedDirection == driver::motor::Direction::Forward
                ? MotorState::DrivingForward
                : MotorState::DrivingReverse;
            duty = std::abs(myPlannedDrive.duty);
            myDrivenDirection = wantedDirection;
            myHasDriven = true;
        }
    }

    bool outputOk{!myActuatorFault}; // Accumulate motor and servo write results.
    if (outputOk && (wanted != myAppliedState || duty != myAppliedDuty))
    {
        switch (wanted)
        {
            case MotorState::Braked:
            case MotorState::Braking: outputOk = myMotor->stop(driver::motor::StopMode::Brake); break;
            case MotorState::NoDrive: outputOk = myMotor->stop(driver::motor::StopMode::Coast); break;
            case MotorState::DrivingForward:
            case MotorState::DrivingReverse:
                outputOk = myMotor->setDirection(wantedDirection) && myMotor->setDuty(duty);
                break;
        }
        if (outputOk) { myAppliedState = wanted; myAppliedDuty = duty; }
    }
    // Disarmed cars centre the steering; a drive timeout keeps the last command.
    if (outputOk && myPlannedDrive.steeringCommand != myAppliedSteering)
    {
        outputOk = mySteering->setDirection(myPlannedDrive.steeringCommand);
        if (outputOk) { myAppliedSteering = myPlannedDrive.steeringCommand; }
    }
    if (!outputOk && !myActuatorFault)
    {
        myMotor->stop(driver::motor::StopMode::Brake);
        myBrake->write(true);
        myControl.forceDisarm(app::runtime::StateReason::ActuatorFault);
        myAppliedState = MotorState::Braked;
        myAppliedDuty = 0.0F;
        myActuatorFault = true;
        ESP_LOGE("FORD", "Actuator error; brake held until reboot");
    }
}

void FordLogic::publishState(const std::uint32_t nowMs) noexcept
{
    // Echo what the car actually applies, so the page can compare it with its sliders.
    const bool driving = myAppliedState == MotorState::DrivingForward
        || myAppliedState == MotorState::DrivingReverse;
    const bool operatorDriven =
        myControl.configuration().driveStyle == navigation::DriveStyle::ManualByRemote;
    mySnapshot.speedCommand =
        (driving && operatorDriven) ? myManualByRemote.lastDrive.speedCommand : 0.0F;
    mySnapshot.steeringDegrees = mySteering->getDirection();
    mySnapshot.forwardDuty = myAppliedState == MotorState::DrivingForward ? myAppliedDuty : 0.0F;
    mySnapshot.backwardDuty = myAppliedState == MotorState::DrivingReverse ? myAppliedDuty : 0.0F;
    mySnapshot.motorState = toString(myAppliedState);
    if (myOdometer)
    {
        // An odometer cannot see direction, so tell it what the motor was told. Only
        // while actually driving: coasting keeps the last known direction, which is
        // the best guess available, and a standstill discards the phase anyway.
        if (myAppliedState == MotorState::DrivingForward) { myOdometer->setForward(true); }
        else if (myAppliedState == MotorState::DrivingReverse) { myOdometer->setForward(false); }

        // Recovers which gap the wheel is in; returns at once if nothing is new.
        myOdometer->update();

        mySnapshot.measuredSpeedMs = myOdometer->speed();
        mySnapshot.odometerDistanceM = myOdometer->distance();
        mySnapshot.measuredSpeedSource = speedSourceName(myOdometer->speedSource());
        mySnapshot.odometerPhaseLosses = myOdometer->phaseLossCount();
    }
    if (myPiSerial)
    {
        mySnapshot.piLink = app::runtime::toString(myControl.piLink(nowMs));
        mySnapshot.piLinkDropped = myPiLink.dropped;
    }
    publishCalibrationState();
    publishSpeedCalibrationState();
    if (myPreviousState != myControl.controlState() || myPreviousMotion != myControl.motionState()
        || myPreviousReason != myControl.stateReason())
    { myCommunication->notifyControlStateChanged(); }
    myPreviousState = myControl.controlState();
    myPreviousMotion = myControl.motionState();
    myPreviousReason = myControl.stateReason();
    myCommunication->publishTelemetry(nowMs, mySnapshot, myControl);
}

void FordLogic::run(const std::atomic<bool>& stop) noexcept
{
    if (!initializeDrivers()) { return; }

    ESP_LOGI("FORD", "Ready: ManualByRemote, brake on; waiting for MQTT Start");
    if (mySerial) { mySerial->write(SerialHelpText); }
    while (!stop.load())
    {
        // Monotonic milliseconds; unsigned subtraction handles tick wraparound.
        const auto now = static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        myCommunication->process(now, myControl);
        receivePiLines(now);
        processSerialCommand();

        // Confirming a measured table is an operator action, not part of driving, so it is
        // handled before the pipeline rather than inside a style's decide step.
        if (myControl.takeStoreGapsRequest()) { storeMeasuredGaps(); }

        readSensors(now);
        checkSafeMode();
        decideAction(now);
        executeAction(now);
        publishState(now);
        sendCarLine(now);

        vTaskDelay(std::max<TickType_t>(1U, pdMS_TO_TICKS(10U)));
    }
    myMotor->stop(driver::motor::StopMode::Brake);
    myCommunication->disconnect();
}

} // namespace app::logic
