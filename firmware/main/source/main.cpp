/** @attention Uncomment DRIVER_TEST_MODE to run the local driver test code. */
// #define DRIVER_TEST_MODE

/** @attention Uncomment ODOMETER_TEST_MODE to run a minimal odometer-only test app. */
// #define ODOMETER_TEST_MODE


#if defined(DRIVER_TEST_MODE)

#include "driver/timer/esp32s3.h"
#include "driver/gpio/esp32s3.h"
#include "driver/adc/esp32s3.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#elif defined(ODOMETER_TEST_MODE)

#include <cstdint>
#include <cstdio>

#include "driver/odometer/esp32s3_a3144.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#else

#include "driver/factory/esp32s3.h"
#include "system/logic/logic.h"
#include <atomic>

#endif

   

extern "C" void app_main(void)
{

    #if defined(DRIVER_TEST_MODE)
    // test kod bara för att ha något i main innan vi har gjort factory och logic
    driver::timer::Esp32s3 testTimer;
    driver::adc::Esp32s3 testADC(1U);
    driver::gpio::Esp32s3 testLedGpio(6U, driver::gpio::Direction::Output);

    ESP_LOGI("main", "gpio.isInitialized %d", testLedGpio.isInitialized());
    testADC.init();
    testTimer.setPeriod(1000);
    testTimer.start();
    testLedGpio.write(1);
    ESP_LOGI("main", "testLedGpio state %d", testLedGpio.read());


    while (true)
    {
        if (testTimer.isTimeout())
        {
            testLedGpio.toggle();
            
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    #elif defined(ODOMETER_TEST_MODE)
    // Short-term prototype: starts the odometer and prints "." for every magnet tick.
    // Remove this block (and the ODOMETER_TEST_MODE define above) once no longer needed.
    const driver::odometer::Config config{
        .pin = 18U,                  // D9
        .pulsesPerRevolution = 2U,   // 2 magnets per wheel
        .wheelDiameterM = 0.031F,    // 31 mm wheel
    };
    driver::odometer::Esp32s3A3144 odometer(config);

    if (!odometer.init())
    {
        std::printf("Odometer init failed!\n");
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000U)); }
    }
    std::printf("Odometer init Suceeded!\n");

    const std::int64_t startUs{esp_timer_get_time()};
    std::int64_t lastTickUs{startUs};
    float lastDistanceM{0.0F};
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(5000U));

        const std::int64_t nowUs{esp_timer_get_time()};
        const float totalDistanceM{odometer.distance()};
        const float totalTimeS{static_cast<float>(nowUs - startUs) / 1.0e6F};
        const float tickDistanceM{totalDistanceM - lastDistanceM};
        const float tickTimeS{static_cast<float>(nowUs - lastTickUs) / 1.0e6F};

        const float totalAverageSpeed{(totalTimeS > 0.0F) ? (totalDistanceM / totalTimeS) : 0.0F};
        const float tickAverageSpeed{(tickTimeS > 0.0F) ? (tickDistanceM / tickTimeS) : 0.0F};

        std::printf("Total average speed: %.3f m/s, total distance: %.3f m, "
                     "last tick average speed: %.3f m/s, last tick distance: %.3f m\n",
                     static_cast<double>(totalAverageSpeed), static_cast<double>(totalDistanceM),
                     static_cast<double>(tickAverageSpeed), static_cast<double>(tickDistanceM));
        std::fflush(stdout);

        lastDistanceM = totalDistanceM;
        lastTickUs    = nowUs;
    }
    #else
    std::atomic<bool> stop{false};
    driver::factory::Esp32s3 factory;
    app::logic::Logic logic(factory);
    logic.run(stop);
    #endif

}
