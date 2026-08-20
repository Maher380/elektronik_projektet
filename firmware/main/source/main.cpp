/** @attention Uncomment DRIVER_TEST_MODE to run the local driver test code. */
// #define DRIVER_TEST_MODE


#ifdef DRIVER_TEST_MODE

#include "driver/timer/esp32s3.h"
#include "driver/gpio/esp32s3.h"
#include "driver/adc/esp32s3.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#else

#include "driver/factory/esp32s3.h"
#include "system/logic/logic.h"
#include <atomic>

#endif

   

extern "C" void app_main(void)
{
    
    #ifdef DRIVER_TEST_MODE
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
    #else
    std::atomic<bool> stop{false};
    driver::factory::Esp32s3 factory;
    app::logic::Logic logic(factory);
    logic.run(stop);
    #endif

}
