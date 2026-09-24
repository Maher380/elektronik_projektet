/** @file main.cpp @brief IR, motor, servo and MQTT full-system experiment. */
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include "driver/factory/interface.h"
#include "driver/adc/interface.h"
#include "driver/gpio/interface.h"
#include "driver/ir_sensor/interface.h"
#include "driver/motor/interface.h"
#include "driver/pwm/interface.h"
#include "driver/servo/interface.h"
#include "system/communication/manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#ifndef CNB_SYSTEM_TEST_HOST
#include "driver/factory/esp32s3.h"
#if CONFIG_CNB_ENABLE_MQTT
static_assert(CONFIG_ESP_MAIN_TASK_STACK_SIZE >= 8192,
              "MQTT requires Main task stack size >= 8192 in menuconfig.");
#endif
#endif
namespace
{
// Proven on-car MQTT settings (reference only, not firmware defaults):
// drive_duty=0.44, stop_distance_cm=45, reaction_distance_cm=52,
// loop_interval_ms=20, telemetry_interval_ms=200. Apply through MQTT/web UI.
// Wall correction applies only with a clear forward view. Tune these separately
// from the full steering commands used to choose a route around an obstacle.
constexpr float WallSteeringGain{2.0F}; // steering command degrees per cm
constexpr float WallDeadbandCm{3.0F}; // ignore small side-proximity differences
constexpr float MaxWallSteeringDegrees{30.0F}; // limit gentle wall correction
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
// Raw readings stay separate. NaN deliberately means the reaction cap in this
// experiment, so an unavailable distance does NOT cause a sensor-fault stop.
/**
 * @brief Select a route or proportional wall correction from one sensor sample.
 * @param measured Raw left/front/right distances, including invalid readings.
 * @param config Active distance thresholds received from runtime configuration.
 * @return Capped distances, steering command and obstacle-brake request.
 */
Decision decide(const std::array<float, 3U>& measured, const app::runtime::Configuration& config)
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
            result.angle = std::copysign(std::min(MaxWallSteeringDegrees,
                WallSteeringGain * correction), error);
        }
        // With a forward obstacle, preserve the tested route selection.
        else if (d[0] > d[1] && d[0] > d[2]) { result.angle = -90.0F; }
        else if (d[2] > d[0] && d[2] > d[1]) { result.angle = 90.0F; }
    }
    return result;
}
// The factory only constructs drivers. All navigation and output decisions are
// here in main.cpp. Host tests run this same loop with fake hardware and time.
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
    bool sampled{false}; // Force an initial sample before planning motion.
    // Sample timestamp and config revision trigger timed/immediate resampling.
    std::uint32_t lastSampleMs{0U}, sampledRevision{0U};
    // Last published state: report transitions without waiting for telemetry.
    auto previousState = control.controlState();
    auto previousMotion = control.motionState();
    auto previousReason = control.stateReason();
    // Output cache; sentinel values force the first motor and servo writes.
    float lastDuty{-1.0F}, lastAngle{std::numeric_limits<float>::quiet_NaN()};
    bool lastBrake{false}; // Previous requested electrical braking mode.
    bool actuatorFault{false}; // Latched output failure; restart required to drive again.
    ESP_LOGI("SYSTEM", "Ready: longest clearance with proportional wall correction; waiting for MQTT Start");
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
            decision = decide(snapshot.distancesCm, config);
            snapshot.decisionDistancesCm = decision.distances;
            lastSampleMs = now;
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
        // Both PWM outputs are high during an authorized obstacle brake.
        const bool brake = authorized && decision.blocked && !actuatorFault;
        // A disarm or latched actuator failure suppresses motor drive.
        const float duty = authorized && !actuatorFault ? plannedDuty : 0.0F;
        // Manual servo testing takes priority while runtime keeps the motor stopped.
        const float angle = control.isServoTest() ? control.servoAngleDegrees() : authorized ? decision.angle : 0.0F;
        // MQTT stop/lease checks run every tick even with a long sensor interval.
        // Avoid resetting the servo PWM frequency when its angle has not changed.
        bool outputOk{!actuatorFault}; // Accumulate motor/PWM/servo write results.
        if (!actuatorFault && (duty != lastDuty || brake != lastBrake))
        {
            if (!brake && duty == 0.0F) { sleep->write(false); }
            const auto mode = brake ? driver::motor::StopMode::Brake : driver::motor::StopMode::Coast;
            outputOk = duty > 0.0F ? motor->setSpeed(duty) : motor->stop(mode);
            // Verify both output writes before enabling nSLEEP, preserving
            // the tested main.cpp output sequence.
            const bool forwardOk = forwardPwm->setDuty(brake ? 1.0F : duty);
            const bool backwardOk = backwardPwm->setDuty(brake ? 1.0F : 0.0F);
            outputOk = outputOk && forwardOk && backwardOk;
            if (outputOk) { sleep->write(brake || duty > 0.0F); }
            lastDuty = duty;
            lastBrake = brake;
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
        snapshot.speedCommand = actuatorFault ? 0.0F : duty;
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
#ifndef CNB_SYSTEM_TEST_HOST
/** @brief ESP-IDF entry point: construct board drivers and run the system loop. */
extern "C" void app_main(void)
{
    std::atomic<bool> stop{false}; // Keep running until a local shutdown is requested.
    driver::factory::Esp32s3 factory; // ESP32-S3 implementations for this board.
    runSystemTest(factory, stop);
}
#endif
