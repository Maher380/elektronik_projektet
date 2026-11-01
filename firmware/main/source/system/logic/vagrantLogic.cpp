/** @file vagrantLogic.cpp @brief Vagrant IR, motor, servo and MQTT control. */
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <limits>
#include "system/logic/vagrantLogic.h"
#include "system/logic/motor_kickstart.h"
#include "driver/factory/interface.h"
#include "driver/adc/interface.h"
#include "driver/gpio/interface.h"
#include "driver/start_module/gpio.h"
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

// All navigation uses capped distances. Raw readings are telemetry only.
constexpr float WallTargetDistanceCm{30.0F};
constexpr float WallDeadbandCm{3.0F};
constexpr float WallKp{2.4F};
constexpr float MaxWallSteeringDegrees{45.0F};
constexpr float WallFilterMs{60.0F};
constexpr float ObstacleSteeringDegrees{90.0F};
constexpr float ReverseSteeringDegrees{60.0F};
constexpr float FullSteeringDegrees{90.0F};
constexpr float SteeringRateDegreesPerSecond{480.0F};
constexpr std::uint32_t SteeringSettleMs{80U};
constexpr app::communication::Topics Topics{
    {"cnb/vagrant/telemetry", "cnb/vagrant/config/state", "cnb/vagrant/command/state", "cnb/vagrant/status"},
    {"cnb/vagrant/config/set", "cnb/vagrant/command"}};

struct Decision
{
    std::array<float, 3U> distances{};
    float angle{0.0F};
    float escapeAngle{-ObstacleSteeringDegrees};
    float dutyScale{1.0F};
    bool blocked{false};
    bool escapeRequired{false};
    bool valid{true};
};

struct NavigationState
{
    float filteredError{0.0F};
    float preferredTurnAngle{0.0F};
    bool hasError{false};
    bool routeBlocked{false};
};

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
    return remaining > 0U && remaining < (std::numeric_limits<std::uint32_t>::max() / 2U);
}

Decision decide(const std::array<float, 3U>& measured, const app::runtime::Configuration& config,
                NavigationState& state, std::uint32_t elapsedMs)
{
    Decision result{};
    for (std::size_t i = 0; i < measured.size(); ++i)
    {
        const bool valid = std::isfinite(measured[i]) && measured[i] >= 0.0F;
        result.valid = result.valid && valid;
        result.distances[i] = valid ? std::clamp(measured[i], 0.0F, config.reactionDistanceCm) : 0.0F;
    }
    if (!result.valid)
    {
        result.blocked = true;
        state = {};
        return result;
    }
    const auto& d = result.distances;
    // Equal capped side distances carry no evidence that either side is freer.
    const float sideAngle = d[0] > d[2] ? -ObstacleSteeringDegrees
        : d[2] > d[0] ? ObstacleSteeringDegrees
        : state.preferredTurnAngle != 0.0F ? state.preferredTurnAngle : -ObstacleSteeringDegrees;
    result.escapeAngle = sideAngle;
    const float releaseMargin = std::min(3.0F,
        (config.reactionDistanceCm - config.stopDistanceCm) * 0.5F);
    const float bestClearance = std::max({d[0], d[1], d[2]});
    if (bestClearance <= config.stopDistanceCm) { state.routeBlocked = true; }
    else if (bestClearance >= config.stopDistanceCm + releaseMargin) { state.routeBlocked = false; }
    result.blocked = state.routeBlocked;
    result.escapeRequired = result.blocked;

    if (d[1] >= d[0] && d[1] >= d[2])
    {
        // With an equally good forward route, avoid turning into a side wall.
        const float wallTarget = std::min(WallTargetDistanceCm, config.reactionDistanceCm);
        const float error = std::max(0.0F, wallTarget - d[0]) - std::max(0.0F, wallTarget - d[2]);
        const float dt = static_cast<float>(std::min<std::uint32_t>(std::max<std::uint32_t>(1U, elapsedMs), 100U));
        const float alpha = dt / (WallFilterMs + dt);
        // Never retain a correction toward a newly closer wall.
        if (!state.hasError || error == 0.0F || error * state.filteredError <= 0.0F)
        { state.filteredError = error; }
        else { state.filteredError += alpha * (error - state.filteredError); }
        state.hasError = true;
        const float correction = std::copysign(std::max(0.0F, std::abs(state.filteredError) - WallDeadbandCm),
                                               state.filteredError);
        result.angle = std::clamp(WallKp * correction, -MaxWallSteeringDegrees, MaxWallSteeringDegrees);
    }
    else
    {
        state.hasError = false;
        state.preferredTurnAngle = sideAngle;
        // Small side advantages produce small turns; large differences reach full steering.
        const float advantage = std::max(d[0], d[2]) - d[1];
        const float strength = std::clamp(advantage
            / std::max(1.0F, config.reactionDistanceCm - config.stopDistanceCm), 0.0F, 1.0F);
        result.angle = std::copysign(ObstacleSteeringDegrees * strength, sideAngle);
    }
    return result;
}

// Lock steering only during the reverse pulse; resume using the latest route.
class ReverseManeuver
{
public:
    enum class Phase { Ready, Braking, Preparing, Reversing, Settling };
    float reverseAngle() const noexcept { return -std::copysign(myReverseSteering, myExitAngle); }
    void reset() noexcept
    { myPhase = Phase::Ready; myHasReversed = false; myReverseSteering = ReverseSteeringDegrees; }
    Phase update(std::uint32_t now, bool blocked, float exitAngle,
                 bool reverseSteeringReady, bool forwardSteeringReady) noexcept
    {
        if (myPhase == Phase::Ready && !blocked) { myHasReversed = false; }
        if (myPhase == Phase::Ready && blocked)
        {
            myReverseSteering = myHasReversed ? FullSteeringDegrees : ReverseSteeringDegrees;
            myExitAngle = exitAngle;
            myPhase = Phase::Braking;
            myDeadline = now + 80U;
        }
        else if (myPhase == Phase::Braking && !timeBefore(now, myDeadline))
        { myPhase = Phase::Preparing; myDeadline = now + 100U; }
        else if (myPhase == Phase::Preparing && !timeBefore(now, myDeadline) && reverseSteeringReady)
        { myPhase = Phase::Reversing; myDeadline = now + 300U; }
        else if (myPhase == Phase::Reversing && !timeBefore(now, myDeadline))
        { myHasReversed = true; myPhase = Phase::Settling; myDeadline = now + 100U; }
        else if (myPhase == Phase::Settling && !timeBefore(now, myDeadline) && forwardSteeringReady)
        { myPhase = Phase::Ready; }
        return myPhase;
    }
private:
    Phase myPhase{Phase::Ready};
    std::uint32_t myDeadline{0U};
    float myExitAngle{-ObstacleSteeringDegrees};
    float myReverseSteering{ReverseSteeringDegrees};
    bool myHasReversed{false};
};
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
    std::unique_ptr<app::communication::Manager> communication;
#if CONFIG_CNB_MODE_COMPETITION
    auto startInput = factory.gpioInput(21U); // Nano ESP32 D10.
    if (!startInput || !startInput->isInitialized())
    { ESP_LOGE("SYSTEM", "Start GPIO failed; motor remains asleep"); return; }
    driver::start_module::Gpio startModule{*startInput};
    app::runtime::ConfigurationRequest competitionConfig{};
    competitionConfig.revision = 1U;
    competitionConfig.values.driveDuty = CONFIG_CNB_COMPETITION_DUTY_PERCENT / 100.0F;
    competitionConfig.values.stopDistanceCm = CONFIG_CNB_COMPETITION_STOP_CM;
    competitionConfig.values.reactionDistanceCm = CONFIG_CNB_COMPETITION_REACTION_CM;
    if (control.applyConfiguration(competitionConfig) != app::runtime::ConfigurationResult::Applied)
    { ESP_LOGE("SYSTEM", "Invalid competition configuration; motor remains asleep"); return; }
#else
    communication = std::make_unique<app::communication::Manager>(factory, Topics);
#endif
    app::communication::TelemetrySnapshot snapshot{}; // Latest measurements and outputs.
    Decision decision{}; // Last navigation decision, held between sensor samples.
    NavigationState navigation{};
    bool sampled{false}; // Force an initial sample before planning motion.
    // Sample timestamp and config revision trigger timed/immediate resampling.
    std::uint32_t lastSampleMs{0U}, sampledRevision{0U};
    // Last published state: report transitions without waiting for telemetry.
    auto previousState = control.controlState();
    auto previousMotion = control.motionState();
    auto previousReason = control.stateReason();
    // Output cache; sentinel values force the first motor and servo writes.
    float lastDuty{-1.0F}, lastAngle{std::numeric_limits<float>::quiet_NaN()};
    driver::motor::Direction lastDirection{driver::motor::Direction::Forward};
    bool lastBrake{false}; // Previous requested electrical braking mode.
    bool actuatorFault{false}; // Latched output failure; restart required to drive again.
    ReverseManeuver reverse;
    app::logic::MotorKickstart kickstart;
    float steeringCommand{0.0F}, previousTargetAngle{0.0F};
    std::uint32_t steeringSettledAtMs{0U}, lastPlanMs{0U};
    bool steeringPending{false};
    bool wasEnabled{false};
#if CONFIG_CNB_MODE_COMPETITION
    ESP_LOGI("SYSTEM", "Competition: waiting for D10 low then high; network disabled");
#else
    ESP_LOGI("SYSTEM", "Test: waiting for MQTT Start");
#endif
    while (!stop.load())
    {
        // Monotonic milliseconds; unsigned subtraction handles tick wraparound.
        const auto now = static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        const std::uint32_t elapsedLoopMs = now - lastPlanMs;
        const std::uint32_t steeringElapsedMs = std::min<std::uint32_t>(elapsedLoopMs, 40U);
        lastPlanMs = now;
        if (communication) { communication->process(now, control); }
#if CONFIG_CNB_MODE_COMPETITION
        startModule.update(now);
        const bool enabled = startModule.state() == driver::start_module::State::Started;
#else
        const bool enabled = control.controlState() == app::runtime::ControlState::Armed;
#endif
        if (enabled && !wasEnabled)
        { sampled = false; navigation = {}; reverse.reset(); kickstart.arm(); }
        wasEnabled = enabled;
        const auto& config = control.configuration();
        if (!sampled || now - lastSampleMs >= config.loopIntervalMs
            || sampledRevision != control.configurationRevision() || previousState != control.controlState())
        {
            snapshot.distancesCm = {left->readDistance(), center->readDistance(), right->readDistance()};
            snapshot.adcRaw = {leftAdc->lastRaw(), centerAdc->lastRaw(), rightAdc->lastRaw()};
            const std::uint32_t elapsedMs = sampled ? now - lastSampleMs : config.loopIntervalMs;
            if (sampledRevision != control.configurationRevision())
            { navigation = {}; reverse.reset(); }
            decision = decide(snapshot.distancesCm, config, navigation, elapsedMs);
            snapshot.decisionDistancesCm = decision.distances;
            lastSampleMs = now;
            sampledRevision = control.configurationRevision();
            sampled = true;

        }
        // Route demand is separate from authorization and actual output commands.
        [[maybe_unused]] const float plannedDuty = decision.blocked ? 0.0F : config.driveDuty;
#if CONFIG_CNB_MODE_COMPETITION
        const bool authorized = startModule.state() == driver::start_module::State::Started;
#else
        const bool authorized = control.authorizeAction(now, decision.valid, plannedDuty, decision.blocked);
#endif
        const bool motorTest = control.updateMotorTest(now);
        DrivePlan plan{};
        if (motorTest && !actuatorFault)
        {
            plan.direction = toMotorDirection(control.motorTestDirection());
            plan.duty = control.motorTestDuty();
            reverse.reset();
            navigation = {};
        }
        else if (authorized && !actuatorFault && decision.valid && config.driveDuty > 0.0F)
        {
            using Phase = ReverseManeuver::Phase;
            const bool reverseReady = !steeringPending
                && std::abs(steeringCommand - reverse.reverseAngle()) <= 1.0F;
            const bool forwardReady = !steeringPending && std::abs(steeringCommand - decision.angle) <= 1.0F;
            const auto phase = reverse.update(now, decision.blocked, decision.escapeAngle, reverseReady, forwardReady);
            if (phase == Phase::Braking || phase == Phase::Preparing || phase == Phase::Reversing)
            {
                plan.angle = reverse.reverseAngle();
                plan.brake = phase == Phase::Braking;
                if (phase == Phase::Reversing)
                { plan.direction = driver::motor::Direction::Backward; plan.duty = 1.0F; }
            }
            else if (phase == Phase::Settling)
            {
                plan.angle = decision.angle;
            }
            else
            {
                // Once the reverse pulse ends, choose again using current capped readings.
                plan.angle = decision.angle;
                if (phase == Phase::Ready && !decision.blocked) { plan.duty = config.driveDuty; }
            }
        }
        else { reverse.reset(); navigation = {}; }
        // Manual servo testing takes priority while runtime keeps the motor stopped.
        const float targetAngle = plan.angle;
        if (std::abs(targetAngle - previousTargetAngle) >= 20.0F)
        { steeringPending = true; steeringSettledAtMs = now + SteeringSettleMs; }
        previousTargetAngle = targetAngle;
        const float step = SteeringRateDegreesPerSecond * static_cast<float>(steeringElapsedMs) / 1000.0F;
        steeringCommand += std::clamp(targetAngle - steeringCommand, -step, step);
        if (std::abs(targetAngle - steeringCommand) > 1.0F)
        { steeringSettledAtMs = now + SteeringSettleMs; }
        if (steeringPending && !timeBefore(now, steeringSettledAtMs)) { steeringPending = false; }
        // Apply only after authorization, obstacle handling and steering preparation.
        plan.duty = kickstart.apply(now, plan.duty, plan.direction,
            !motorTest && authorized && decision.valid && !actuatorFault && !plan.brake);
        const float angle = control.isServoTest() ? control.servoAngleDegrees() : steeringCommand;
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
            || previousReason != control.stateReason())
        { if (communication) { communication->notifyControlStateChanged(); } }
        previousState = control.controlState();
        previousMotion = control.motionState();
        previousReason = control.stateReason();
        if (communication) { communication->publishTelemetry(now, snapshot, control); }
        vTaskDelay(std::max<TickType_t>(1U, pdMS_TO_TICKS(10U)));
    }
    sleep->write(false);
    motor->stop(driver::motor::StopMode::Coast);
    if (communication) { communication->disconnect(); }
}
} // namespace

namespace app::logic
{
void VagrantLogic::run(const std::atomic<bool>& stop) noexcept
{
    runSystemTest(myFactory, stop);
}
} // namespace app::logic
