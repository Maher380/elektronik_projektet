#include "system/logic/fordLogic.h"

#include "driver/factory/interface.h"
#include "driver/pwm/interface.h"
#include "driver/servo/interface.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

namespace app::logic
{
void FordLogic::run(const std::atomic<bool>& stop) noexcept
{
    driver::pwm::Config servoConfig{};
    servoConfig.pin = 9U; // D6
    servoConfig.frequencyHz = 50U;
    auto servoPwm = myFactory.pwm(servoConfig);
    if (!servoPwm) { ESP_LOGE("FORD", "Steering PWM allocation failed"); return; }
    // The servo uses the PWM above; declaration order keeps it alive.
    auto servo = myFactory.fordServo(*servoPwm);
    // init() also starts the PWM and centres the steering.
    if (!servo || !servo->init()) { ESP_LOGE("FORD", "Steering initialization failed"); return; }

    // Stay in run() so the servo keeps sweeping from side to side
    ESP_LOGI("FORD", "Ready: steering centred, no drive logic yet; motor remains disabled");
    float direction{0};
    bool shiftingLeftwards{false};
    while (!stop.load()) { 
        servo->setDirection(direction);
        (shiftingLeftwards ? direction-- : direction++);
        if ( 89.0 <= direction)
            shiftingLeftwards = true;
        if (-89.0 >= direction)
            shiftingLeftwards = false;

        vTaskDelay(pdMS_TO_TICKS(100U)); 
    }
    servo->deinit();
}
} // namespace app::logic
