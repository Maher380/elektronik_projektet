/** @file fordLogic.cpp @brief Ford ManualByRemote: an operator drives motor and steering over MQTT. */
#include "system/logic/fordLogic.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

#include "driver/adc/interface.h"
#include "driver/factory/interface.h"
#include "driver/gpio/interface.h"
#include "driver/motor/interface.h"
#include "driver/pwm/interface.h"
#include "driver/servo/interface.h"
#include "driver/voltage_meter/interface.h"
#include "system/communication/manager.h"
#include "system/runtime/control.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

namespace
{
// Pins, see documentation/design_documents/pin_mapping.md.
constexpr std::uint8_t BrakePin{5U};     // D2 -> A89301 BRAKE, high = brake
constexpr std::uint8_t DirectionPin{7U}; // D4 -> A89301 DIR
constexpr std::uint8_t SpeedPin{8U};     // D5 -> A89301 SPD, 20 kHz PWM
constexpr std::uint8_t SteeringPin{9U};  // D6 -> steering servo
constexpr std::uint8_t BatteryAdcPin{4U}; // A3 <- drive battery divider joint
constexpr std::uint32_t SteeringPwmFrequencyHz{50U};

// Calibration, compiled in until it moves to NVS.
/** Duty for speed command ±100: about 1.5 m/s unloaded. Raise only after the load test. */
constexpr float TopSpeedDuty{0.15F};
/** Duty for speed command ±1: the lowest demand that starts the motor from standstill. */
constexpr float StartDuty{0.08F};
/** How long the car brakes before it drives in the other direction. */
constexpr std::uint32_t DirectionChangeBrakeMs{300U};
/** No drive command for this long gives no drive; the car stays armed. */
constexpr std::uint32_t DriveTimeoutMs{500U};
/** True if DIR low drives the car forward with this motor's phase wiring. */
constexpr bool InvertMotorDirection{false};
/** Measured battery divider resistors (nominal 100 kΩ and 33 kΩ, 150 nF from A3 to GND). */
constexpr float BatteryR1Ohm{101240.0F};
constexpr float BatteryR2Ohm{32990.0F};
/** Battery read period; the meter averages its last 16 reads, so about 1.6 s. */
constexpr std::uint32_t BatteryReadIntervalMs{100U};

// Publication and command channels shared with the operator console.
// No config/set subscription: every Ford setting is compiled in.
constexpr app::communication::Topics Topics{
    {"cnb/ford/telemetry", "cnb/ford/config/state", "cnb/ford/command/state", "cnb/ford/status"},
    {nullptr, "cnb/ford/command"}};

// Car network: the operator laptop's hotspot, so the broker is always at 192.168.137.1.
// Compiled in, passwords included, until it moves to NVS (see nvs_usage.md).
constexpr app::communication::NetworkSettings Network{
    "cnb-net", "cnbrules",
    "mqtt://192.168.137.1:1883", "cnb-ford", "cnb-ford", "cnb"};

/** What the motor is doing, reported in telemetry as motor.state. */
enum class MotorState : std::uint8_t { Braked, NoDrive, Braking, DrivingForward, DrivingReverse };

const char* toString(MotorState state) noexcept
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

/**
 * @brief Turn a speed command into motor duty, skipping the duty band where the motor does not start.
 * @param speedCommand −100 full reverse, 0 no drive, +100 full forward.
 * @return 0 for no drive, otherwise StartDuty to TopSpeedDuty.
 * @todo Add host tests for the mapping and its clamping.
 */
float dutyFor(float speedCommand) noexcept
{
    const float magnitude = std::min(std::abs(speedCommand), 100.0F);
    if (magnitude <= 0.0F) { return 0.0F; }
    return StartDuty + (TopSpeedDuty - StartDuty) * (std::max(magnitude, 1.0F) - 1.0F) / 99.0F;
}

/**
 * @brief Run the ManualByRemote loop until stop is set.
 * @param factory Owns construction of the board-specific drivers.
 * @param stop Cooperative shutdown flag; the operator's panic stop is handled by MQTT.
 * @todo Add host tests for the direction-change brake and the drive/heartbeat timeouts.
 */
void runManualByRemote(driver::factory::Interface& factory, const std::atomic<bool>& stop)
{
    // Brake before anything else, so the wheels cannot spin while Wi-Fi and MQTT start.
    // If start-up fails, the pin is released and the BRAKE pull-up keeps the brake on.
    auto brake = factory.gpioOutput(BrakePin);
    if (!brake || !brake->isInitialized()) { ESP_LOGE("FORD", "Brake GPIO failed"); return; }
    brake->write(true);
    auto direction = factory.gpioOutput(DirectionPin);
    auto speedPwm = factory.pwm(SpeedPin);
    driver::pwm::Config steeringConfig{};
    steeringConfig.pin = SteeringPin;
    steeringConfig.frequencyHz = SteeringPwmFrequencyHz;
    auto steeringPwm = factory.pwm(steeringConfig);
    if (!direction || !direction->isInitialized() || !speedPwm || !steeringPwm)
    { ESP_LOGE("FORD", "Driver allocation failed"); return; }
    // Wrappers use the drivers above; declaration order keeps them alive.
    auto motor = factory.fordMotor(*speedPwm, *direction, *brake, InvertMotorDirection);
    auto steering = factory.fordServo(*steeringPwm);
    // A89301::init() releases BRAKE with SPD at 0, so brake again straight after it.
    if (!motor || !steering || !motor->init() || !motor->stop(driver::motor::StopMode::Brake)
        || !steering->init())
    { brake->write(true); ESP_LOGE("FORD", "Initialization failed; brake held"); return; }
    // The battery meter is optional: the car drives without it, and telemetry leaves it out.
    auto batteryAdc = factory.adc(BatteryAdcPin);
    std::unique_ptr<driver::voltage_meter::Interface> battery{};
    if (batteryAdc) { battery = factory.voltageMeter(*batteryAdc, BatteryR1Ohm, BatteryR2Ohm); }
    if (!battery || !batteryAdc->init() || !battery->isInitialized())
    { ESP_LOGW("FORD", "Battery meter failed; battery voltage not reported"); }

    app::runtime::Control control{false, true}; // ManualByRemote is Ford's only drive style.
    app::communication::Manager communication{factory, Topics, Network}; // Wi-Fi/MQTT lifecycle.
    app::communication::TelemetrySnapshot snapshot{}; // No distance sensors: they stay null.
    // Last published state: report transitions without waiting for telemetry.
    auto previousState = control.controlState();
    auto previousMotion = control.motionState();
    auto previousReason = control.stateReason();
    // Applied outputs; the motor is braked after start-up.
    MotorState appliedState{MotorState::Braked};
    float appliedDuty{0.0F};
    float appliedSteering{std::numeric_limits<float>::quiet_NaN()};
    // Direction-change brake: the last driven direction and when the brake started.
    auto drivenDirection = driver::motor::Direction::Forward;
    bool hasDriven{false};
    bool braking{false};
    std::uint32_t brakeStartMs{0U};
    bool actuatorFault{false}; // Latched output failure; restart required to drive again.
    bool batteryRead{false};
    std::uint32_t lastBatteryReadMs{0U};
    ESP_LOGI("FORD", "Ready: ManualByRemote, brake on; waiting for MQTT Start");
    while (!stop.load())
    {
        // Monotonic milliseconds; unsigned subtraction handles tick wraparound.
        const auto now = static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        communication.process(now, control);
        // 0/0 while disarmed; speed 0 after a drive timeout.
        const auto drive = control.remoteDrive(now, DriveTimeoutMs);
        const bool armed = control.controlState() == app::runtime::ControlState::Armed;

        MotorState wanted{MotorState::Braked};
        float duty{0.0F};
        const auto wantedDirection = drive.speedCommand < 0.0F ? driver::motor::Direction::Backward
                                                               : driver::motor::Direction::Forward;
        if (!armed || actuatorFault) { braking = false; }
        else if (drive.speedCommand == 0.0F) { wanted = MotorState::NoDrive; braking = false; }
        else
        {
            // Never reverse a spinning sensorless motor: brake first, then change DIR.
            const bool reversing = hasDriven && wantedDirection != drivenDirection;
            if (!reversing) { braking = false; }
            else if (!braking) { braking = true; brakeStartMs = now; }
            if (braking && (now - brakeStartMs) < DirectionChangeBrakeMs) { wanted = MotorState::Braking; }
            else
            {
                braking = false;
                wanted = wantedDirection == driver::motor::Direction::Forward ? MotorState::DrivingForward
                                                                              : MotorState::DrivingReverse;
                duty = dutyFor(drive.speedCommand);
                drivenDirection = wantedDirection;
                hasDriven = true;
            }
        }

        bool outputOk{!actuatorFault}; // Accumulate motor and servo write results.
        if (outputOk && (wanted != appliedState || duty != appliedDuty))
        {
            switch (wanted)
            {
                case MotorState::Braked:
                case MotorState::Braking: outputOk = motor->stop(driver::motor::StopMode::Brake); break;
                case MotorState::NoDrive: outputOk = motor->stop(driver::motor::StopMode::Coast); break;
                case MotorState::DrivingForward:
                case MotorState::DrivingReverse:
                    outputOk = motor->setDirection(wantedDirection) && motor->setSpeed(duty);
                    break;
            }
            if (outputOk) { appliedState = wanted; appliedDuty = duty; }
        }
        // Disarmed cars centre the steering; a drive timeout keeps the last command.
        if (outputOk && drive.steeringCommand != appliedSteering)
        {
            outputOk = steering->setDirection(drive.steeringCommand);
            if (outputOk) { appliedSteering = drive.steeringCommand; }
        }
        if (!outputOk && !actuatorFault)
        {
            motor->stop(driver::motor::StopMode::Brake);
            brake->write(true);
            control.forceDisarm(app::runtime::StateReason::ActuatorFault);
            appliedState = MotorState::Braked;
            appliedDuty = 0.0F;
            actuatorFault = true;
            ESP_LOGE("FORD", "Actuator error; brake held until reboot");
        }

        // Echo what the car actually applies, so the page can compare it with its sliders.
        const bool driving = appliedState == MotorState::DrivingForward
            || appliedState == MotorState::DrivingReverse;
        snapshot.speedCommand = driving ? drive.speedCommand : 0.0F;
        snapshot.steeringDegrees = steering->getDirection();
        snapshot.forwardDuty = appliedState == MotorState::DrivingForward ? appliedDuty : 0.0F;
        snapshot.backwardDuty = appliedState == MotorState::DrivingReverse ? appliedDuty : 0.0F;
        snapshot.motorState = toString(appliedState);
        if (battery && (!batteryRead || (now - lastBatteryReadMs) >= BatteryReadIntervalMs))
        {
            snapshot.batteryVoltage = battery->readVoltage();
            lastBatteryReadMs = now;
            batteryRead = true;
        }
        if (previousState != control.controlState() || previousMotion != control.motionState()
            || previousReason != control.stateReason()) { communication.notifyControlStateChanged(); }
        previousState = control.controlState();
        previousMotion = control.motionState();
        previousReason = control.stateReason();
        communication.publishTelemetry(now, snapshot, control);
        vTaskDelay(std::max<TickType_t>(1U, pdMS_TO_TICKS(10U)));
    }
    motor->stop(driver::motor::StopMode::Brake);
    communication.disconnect();
}
} // namespace

namespace app::logic
{
void FordLogic::run(const std::atomic<bool>& stop) noexcept
{
    runManualByRemote(myFactory, stop);
}
} // namespace app::logic
