/** @file vagrantLogic.cpp @brief Vagrant IR, motor, servo and MQTT control. */
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <limits>
#include "system/logic/vagrantLogic.h"
#include "system/logic/reverse_controller.h"
#include "driver/factory/interface.h"
#include "driver/adc/interface.h"
#include "driver/gpio/interface.h"
#include "driver/distance_sensor/interface.h"
#include "driver/motor/interface.h"
#include "driver/pwm/interface.h"
#include "driver/servo/interface.h"
#include "system/communication/manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
namespace
{
driver::motor::Direction toMotorDirection(app::runtime::MotorTestDirection direction) noexcept
{
    return direction == app::runtime::MotorTestDirection::Backward
        ? driver::motor::Direction::Backward
        : driver::motor::Direction::Forward;
}

// Proven on-car MQTT settings (reference only, not firmware defaults):
// drive_duty=0.44, stop_distance_cm=45, reaction_distance_cm=52,
// loop_interval_ms=20, telemetry_interval_ms=200. Apply through MQTT/web UI.
// Wall correction applies only with a clear forward view. Tune these separately
// from the full steering commands used to choose a route around an obstacle.
constexpr float WallPidKp{2.0F}; // steering command degrees per cm
constexpr float WallPidKi{0.0F}; // keep integral disabled until the car is proven stable
constexpr float WallPidKd{0.4F}; // damps quick left/right sensor changes
constexpr float WallIntegralLimitCmMs{250.0F};
constexpr float WallDeadbandCm{3.0F}; // ignore small side-proximity differences
constexpr float MaxWallSteeringDegrees{30.0F}; // limit gentle wall correction
constexpr float ReverseDuty{1.0F}; // short kick pulse; the car needs high duty to move
constexpr std::uint32_t KickStartMs{180U}; // overcome static friction before returning to drive_duty
// Publication and command channels shared with the operator tools.
constexpr app::communication::Topics Topics{
    {"cnb/vagrant/telemetry", "cnb/vagrant/config/state", "cnb/vagrant/command/state", "cnb/vagrant/status"},
    {"cnb/vagrant/config/set", "cnb/vagrant/command"}};
/** One navigation result, independent of MQTT authorization. */
struct Decision
{
    std::array<float, 3U> distances{}; ///< Capped left/front/right distances in cm.
    float angle{0.0F}; ///< Steering command: negative left, positive right.
    bool blocked{false}; ///< All three directions are within the stop threshold.
};

/** Memory for the wall-following PID correction. */
struct WallPid
{
    float previousError{0.0F};
    float integral{0.0F};
    bool hasPrevious{false};
};

/** Complete actuator intent after safety and escape handling. */
struct DrivePlan
{
    driver::motor::Direction direction{driver::motor::Direction::Forward};
    float duty{0.0F};
    float angle{0.0F};
    bool brake{false};
};

bool timeBefore(std::uint32_t nowMs, std::uint32_t deadlineMs) noexcept
{
    const std::uint32_t remaining = deadlineMs - nowMs;
    return remaining > 0U
        && remaining < (std::numeric_limits<std::uint32_t>::max() / 2U);
}
// Raw readings stay separate. NaN deliberately means the reaction cap in this
// experiment, so an unavailable distance does NOT cause a sensor-fault stop.
/**
 * @brief Select a route or proportional wall correction from one sensor sample.
 * @param measured Raw left/front/right distances, including invalid readings.
 * @param config Active distance thresholds received from runtime configuration.
 * @return Capped distances, steering command and obstacle-brake request.
 */
Decision decide(const std::array<float, 3U>& measured, const app::runtime::Configuration& config,
                WallPid& wallPid, std::uint32_t elapsedMs)
{
    Decision result{}; // Defaults to centered steering without an obstacle stop.
    for (std::size_t i = 0; i < measured.size(); ++i)
    {
        result.distances[i] = !std::isfinite(measured[i]) ? config.reactionDistanceCm
            : std::clamp(measured[i], 0.0F, config.reactionDistanceCm);
    }
    const auto& d = result.distances; // left, forward, right
    result.blocked = d[0] <= config.stopDistanceCm && d[1] <= config.stopDistanceCm && d[2] <= config.stopDistanceCm;
    if (!result.blocked)
    {
        if (d[1] >= config.reactionDistanceCm)
        {
            // Use the stop distance as the side-clearance target. Far-away
            // readings exert no pull; equal proximity on both sides cancels.
            const float leftProximity = std::max(0.0F, config.stopDistanceCm - d[0]);
            const float rightProximity = std::max(0.0F, config.stopDistanceCm - d[2]);
            // Positive error means the left wall is closer: steer right.
            const float error = leftProximity - rightProximity;
            // Subtract the deadband so steering starts smoothly at its boundary.
            const float correction = std::max(0.0F, std::abs(error) - WallDeadbandCm);
            const float pidError = std::copysign(correction, error);
            const float dtMs = static_cast<float>(std::max<std::uint32_t>(1U, elapsedMs));
            wallPid.integral = std::clamp(wallPid.integral + pidError * dtMs,
                                          -WallIntegralLimitCmMs,
                                          WallIntegralLimitCmMs);
            const float derivative = wallPid.hasPrevious
                ? (pidError - wallPid.previousError) / dtMs
                : 0.0F;
            const float command = (WallPidKp * pidError)
                + (WallPidKi * wallPid.integral)
                + (WallPidKd * derivative * 100.0F);
            result.angle = std::clamp(command, -MaxWallSteeringDegrees,
                                      MaxWallSteeringDegrees);
            wallPid.previousError = pidError;
            wallPid.hasPrevious = true;
        }
        // With a forward obstacle, preserve the tested route selection.
        else
        {
            wallPid = {};
            if (d[0] > d[1] && d[0] > d[2]) { result.angle = -90.0F; }
            else if (d[2] > d[0] && d[2] > d[1]) { result.angle = 90.0F; }
        }
    }
    else { wallPid = {}; }
    return result;
}
// The factory only constructs drivers. Host tests run this same loop with fake hardware and time.
/**
 * @brief Run the integrated sensor, actuator and MQTT loop until stop is set.
 * @param factory Owns construction of the board-specific drivers.
 * @param stop Cooperative shutdown flag; normal operator Stop is handled by MQTT.
 */
void runSystemTest(driver::factory::Interface& factory, const std::atomic<bool>& stop)
{
    auto sleep = factory.gpioOutput(7U); // D4 -> nSLEEP
    if (!sleep || !sleep->isInitialized()) { ESP_LOGE("SYSTEM", "Sleep GPIO failed"); return; }
    sleep->write(false);
    auto forwardPwm = factory.pwm(5U);  // D2 -> MP6550 IN1
    auto backwardPwm = factory.pwm(6U); // D3 -> MP6550 IN2
    driver::pwm::Config servoConfig{}; // Frequency-controlled steering output.
    servoConfig.pin = 9U; // D6
    servoConfig.frequencyHz = 330U;
    auto servoPwm = factory.pwm(servoConfig);
    auto leftAdc = factory.adc(1U);   // A0
    auto centerAdc = factory.adc(2U); // A1
    auto rightAdc = factory.adc(4U);  // A3
    if (!forwardPwm || !backwardPwm || !servoPwm || !leftAdc || !centerAdc || !rightAdc)
    { ESP_LOGE("SYSTEM", "Driver allocation failed"); return; }
    // Wrappers use the PWM/ADC drivers above; declaration order keeps them alive.
    auto motor = factory.motor(*forwardPwm, *backwardPwm);
    auto servo = factory.servo(*servoPwm);
    auto left = factory.ir_sensor(*leftAdc);
    auto center = factory.ir_sensor(*centerAdc);
    auto right = factory.ir_sensor(*rightAdc);
    if (!motor || !servo || !left || !center || !right
        || !leftAdc->init() || !centerAdc->init() || !rightAdc->init()
        || !motor->init() || !servo->init()
        || !motor->setDirection(driver::motor::Direction::Forward))
    { ESP_LOGE("SYSTEM", "Initialization failed; motor remains asleep"); return; }
    app::runtime::Control control{true}; // Enable system-test MQTT config and servo commands.
    app::communication::Manager communication{factory, Topics}; // Wi-Fi/MQTT lifecycle.
    app::communication::TelemetrySnapshot snapshot{}; // Latest measurements and outputs.
    Decision decision{}; // Last navigation decision, held between sensor samples.
    WallPid wallPid{}; // PID memory for gentle wall correction.
    bool sampled{false}; // Force an initial sample before planning motion.
    // Sample timestamp and config revision trigger timed/immediate resampling.
    std::uint32_t lastSampleMs{0U}, sampledRevision{0U}, sampleSequence{0U};
    // Last published state: report transitions without waiting for telemetry.
    auto previousState = control.controlState();
    auto previousMotion = control.motionState();
    auto previousReason = control.stateReason();
    // Output cache; sentinel values force the first motor and servo writes.
    float lastDuty{-1.0F}, lastAngle{std::numeric_limits<float>::quiet_NaN()};
    driver::motor::Direction lastDirection{driver::motor::Direction::Forward};
    bool lastBrake{false}; // Previous requested electrical braking mode.
    bool actuatorFault{false}; // Latched output failure; restart required to drive again.
    app::logic::ReverseController reverse;
    std::uint32_t kickStartUntilMs{0U};
    ESP_LOGI("SYSTEM", "Ready: longest clearance with PID wall correction; waiting for MQTT Start");
    while (!stop.load())
    {
        // Monotonic milliseconds; unsigned subtraction handles tick wraparound.
        const auto now = static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        communication.process(now, control);
        const auto& config = control.configuration();
        if (!sampled || now - lastSampleMs >= config.loopIntervalMs
            || sampledRevision != control.configurationRevision() || previousState != control.controlState())
        {
            snapshot.distancesCm = {left->readDistance(), center->readDistance(), right->readDistance()};
            snapshot.adcRaw = {leftAdc->lastRaw(), centerAdc->lastRaw(), rightAdc->lastRaw()};
            const std::uint32_t elapsedMs = sampled ? now - lastSampleMs : config.loopIntervalMs;
            decision = decide(snapshot.distancesCm, config, wallPid, elapsedMs);
            snapshot.decisionDistancesCm = decision.distances;
            lastSampleMs = now;
            ++sampleSequence;
            sampledRevision = control.configurationRevision();
            sampled = true;
            ESP_LOGI("IR", "cm L/C/R %.2f / %.2f / %.2f | ADC %ld / %ld / %ld | decision %.2f / %.2f / %.2f",
                static_cast<double>(snapshot.distancesCm[0]), static_cast<double>(snapshot.distancesCm[1]),
                static_cast<double>(snapshot.distancesCm[2]), static_cast<long>(snapshot.adcRaw[0]),
                static_cast<long>(snapshot.adcRaw[1]), static_cast<long>(snapshot.adcRaw[2]),
                static_cast<double>(decision.distances[0]), static_cast<double>(decision.distances[1]),
                static_cast<double>(decision.distances[2]));
        }
        // Route demand is separate from authorization and actual output commands.
        const float plannedDuty = decision.blocked ? 0.0F : config.driveDuty;
        // true declares the capped sample usable under the tested NaN policy.
        const bool authorized = control.authorizeAction(now, true, plannedDuty, decision.blocked);
        const bool motorTest = control.updateMotorTest(now);
        DrivePlan plan{};
        if (motorTest && !actuatorFault)
        {
            plan.direction = toMotorDirection(control.motorTestDirection());
            plan.duty = control.motorTestDuty();
            plan.angle = 0.0F;
            kickStartUntilMs = 0U;
            reverse.reset();
        }
        else if (authorized && !actuatorFault)
        {
            // No rear sensor: one retry is allowed, then hold until forward
            // clearance is confirmed by two new measurements.
            const auto phase = reverse.update(now, sampleSequence, decision.blocked,
                                              decision.distances[1] >= config.reactionDistanceCm);
            if (phase == app::logic::ReverseController::Phase::Braking)
            {
                plan.brake = true;
            }
            else if (phase == app::logic::ReverseController::Phase::Reversing)
            {
                plan.direction = driver::motor::Direction::Backward;
                plan.duty = ReverseDuty;
            }
            else if (phase == app::logic::ReverseController::Phase::Ready && !decision.blocked)
            {
                plan.duty = plannedDuty;
                plan.angle = decision.angle;
            }
        }
        else { reverse.reset(); }
        // A disarm or latched actuator failure suppresses motor drive and arms a
        // fresh kickstart for the next forward start.
        if (!motorTest && plan.direction == driver::motor::Direction::Forward && plan.duty > 0.0F
            && (lastDuty <= 0.0F || lastDirection != driver::motor::Direction::Forward)
            && kickStartUntilMs == 0U)
        {
            kickStartUntilMs = now + KickStartMs;
        }
        if (!motorTest && plan.direction == driver::motor::Direction::Forward && plan.duty > 0.0F
            && kickStartUntilMs != 0U)
        {
            if (timeBefore(now, kickStartUntilMs))
            {
                plan.duty = 1.0F;
            }
            else { kickStartUntilMs = 0U; }
        }
        if (plan.direction != driver::motor::Direction::Forward || plan.duty <= 0.0F)
        {
            kickStartUntilMs = 0U;
        }
        // Manual servo testing takes priority while runtime keeps the motor stopped.
        const float angle = control.isServoTest() ? control.servoAngleDegrees() : plan.angle;
        // MQTT stop/lease checks run every tick even with a long sensor interval.
        // Avoid resetting the servo PWM frequency when its angle has not changed.
        bool outputOk{!actuatorFault}; // Accumulate motor/PWM/servo write results.
        if (!actuatorFault && (plan.duty != lastDuty || plan.brake != lastBrake
            || plan.direction != lastDirection))
        {
            if (plan.duty == 0.0F || plan.direction != lastDirection) { sleep->write(false); }
            outputOk = motor->setDirection(plan.direction);
            const auto mode = plan.brake ? driver::motor::StopMode::Brake : driver::motor::StopMode::Coast;
            outputOk = outputOk && (plan.duty > 0.0F ? motor->setSpeed(plan.duty) : motor->stop(mode));
            // Verify both output writes before enabling nSLEEP, preserving
            // the tested Vagrant output sequence.
            const float forwardDuty = plan.brake ? 1.0F
                : plan.direction == driver::motor::Direction::Forward ? plan.duty : 0.0F;
            const float backwardDuty = plan.brake ? 1.0F
                : plan.direction == driver::motor::Direction::Backward ? plan.duty : 0.0F;
            const bool forwardOk = forwardPwm->setDuty(forwardDuty);
            const bool backwardOk = backwardPwm->setDuty(backwardDuty);
            outputOk = outputOk && forwardOk && backwardOk;
            if (outputOk) { sleep->write(plan.brake || plan.duty > 0.0F); }
            lastDuty = plan.duty;
            lastBrake = plan.brake;
            lastDirection = plan.direction;
        }
        if (outputOk && angle != lastAngle)
        {
            outputOk = servo->setDirection(angle);
            if (outputOk) { lastAngle = angle; }
        }
        if (!outputOk && !actuatorFault)
        {
            sleep->write(false);
            motor->stop(driver::motor::StopMode::Coast);
            control.forceDisarm(app::runtime::StateReason::ActuatorFault);
            actuatorFault = true;
            ESP_LOGE("SYSTEM", "Actuator error; motor disabled until reboot");
        }
        snapshot.speedCommand = actuatorFault ? 0.0F : plan.duty;
        snapshot.steeringDegrees = servo->getDirection();
        snapshot.forwardDuty = forwardPwm->duty();
        snapshot.backwardDuty = backwardPwm->duty();
        if (previousState != control.controlState() || previousMotion != control.motionState()
            || previousReason != control.stateReason()) { communication.notifyControlStateChanged(); }
        previousState = control.controlState();
        previousMotion = control.motionState();
        previousReason = control.stateReason();
        communication.publishTelemetry(now, snapshot, control);
        vTaskDelay(std::max<TickType_t>(1U, pdMS_TO_TICKS(10U)));
    }
    sleep->write(false);
    motor->stop(driver::motor::StopMode::Coast);
    communication.disconnect();
}
} // namespace

namespace app::logic
{
void VagrantLogic::run(const std::atomic<bool>& stop) noexcept
{
    runSystemTest(myFactory, stop);
}
} // namespace app::logic
