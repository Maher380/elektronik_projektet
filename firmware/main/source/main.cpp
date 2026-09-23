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
constexpr app::communication::Topics Topics{
    {"cnb/vagrant/telemetry", "cnb/vagrant/config/state", "cnb/vagrant/command/state", "cnb/vagrant/status"},
    {"cnb/vagrant/config/set", "cnb/vagrant/command"}};
struct Decision
{
    std::array<float, 3U> distances{};
    float angle{0.0F};
    bool blocked{false};
};
// Raw readings stay separate. NaN deliberately means the reaction cap in this
// experiment, so an unavailable distance does NOT cause a sensor-fault stop.
Decision decide(const std::array<float, 3U>& measured, const app::runtime::Configuration& config)
{
    Decision result{};
    for (std::size_t i = 0; i < measured.size(); ++i)
    {
        result.distances[i] = !std::isfinite(measured[i]) ? config.reactionDistanceCm
            : std::clamp(measured[i], 0.0F, config.reactionDistanceCm);
    }
    const auto& d = result.distances; // left, forward, right
    result.blocked = d[0] <= config.stopDistanceCm && d[1] <= config.stopDistanceCm && d[2] <= config.stopDistanceCm;
    if (!result.blocked)
    {
        // Only a unique longest side turns. A forward maximum or tied maxima centre.
        if (d[0] > d[1] && d[0] > d[2]) { result.angle = -90.0F; }
        else if (d[2] > d[0] && d[2] > d[1]) { result.angle = 90.0F; }
    }
    return result;
}
// The factory only constructs drivers. All navigation and output decisions are
// here in main.cpp. Host tests run this same loop with fake hardware and time.
void runSystemTest(driver::factory::Interface& factory, const std::atomic<bool>& stop)
{
    auto sleep = factory.gpioOutput(7U); // D4 -> nSLEEP
    if (!sleep || !sleep->isInitialized()) { ESP_LOGE("SYSTEM", "Sleep GPIO failed"); return; }
    sleep->write(false);
    auto forwardPwm = factory.pwm(5U);  // D2 -> MP6550 IN1
    auto backwardPwm = factory.pwm(6U); // D3 -> MP6550 IN2
    driver::pwm::Config servoConfig{};
    servoConfig.pin = 9U; // D6
    servoConfig.frequencyHz = 330U;
    auto servoPwm = factory.pwm(servoConfig);
    auto leftAdc = factory.adc(1U);   // A0
    auto centerAdc = factory.adc(2U); // A1
    auto rightAdc = factory.adc(4U);  // A3
    if (!forwardPwm || !backwardPwm || !servoPwm || !leftAdc || !centerAdc || !rightAdc)
    { ESP_LOGE("SYSTEM", "Driver allocation failed"); return; }
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
    app::runtime::Control control{true};
    app::communication::Manager communication{factory, Topics};
    app::communication::TelemetrySnapshot snapshot{};
    Decision decision{};
    bool sampled{false};
    std::uint32_t lastSampleMs{0U}, sampledRevision{0U};
    auto previousState = control.controlState();
    auto previousMotion = control.motionState();
    auto previousReason = control.stateReason();
    float lastDuty{-1.0F}, lastAngle{std::numeric_limits<float>::quiet_NaN()};
    bool lastBrake{false}, actuatorFault{false};
    ESP_LOGI("SYSTEM", "Ready: longest clearance, ties straight; waiting for MQTT Start");
    while (!stop.load())
    {
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
        const float plannedDuty = decision.blocked ? 0.0F : config.driveDuty;
        const bool authorized = control.authorizeScrum16Action(now, true, plannedDuty, decision.blocked);
        const bool brake = authorized && decision.blocked && !actuatorFault;
        const float duty = authorized && !actuatorFault ? plannedDuty : 0.0F;
        const float angle = control.isServoTest() ? control.servoAngleDegrees() : authorized ? decision.angle : 0.0F;
        // MQTT stop/lease checks run every tick even with a long sensor interval.
        // Avoid resetting the servo PWM frequency when its angle has not changed.
        bool outputOk{!actuatorFault};
        if (!actuatorFault && (duty != lastDuty || brake != lastBrake))
        {
            if (!brake && duty == 0.0F) { sleep->write(false); }
            const auto mode = brake ? driver::motor::StopMode::Brake : driver::motor::StopMode::Coast;
            outputOk = duty > 0.0F ? motor->setSpeed(duty) : motor->stop(mode);
            // The MP6550 wrapper does not propagate PWM errors; verify the writes
            // directly as well, before enabling nSLEEP.
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
extern "C" void app_main(void)
{
    std::atomic<bool> stop{false};
    driver::factory::Esp32s3 factory;
    runSystemTest(factory, stop);
}
#endif