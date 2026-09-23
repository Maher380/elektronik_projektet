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
#include <cstring>

#include "driver/gpio/esp32s3.h"
#include "driver/odometer/a3144.h"
#include "driver/serial/esp32s3.h"
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
    // Odometer-only test app. Prints a line for every counted pulse and a status
    // line every second. Commands (type + Enter): r = reset, i = init, d = deinit, h = help.
    // Remove this block (and the ODOMETER_TEST_MODE define above) once no longer needed.
    constexpr std::uint8_t odometerPin{18U};           // D9 / GPIO18
    constexpr std::uint32_t pollPeriodMs{10U};         // How often pulses and commands are checked.
    constexpr std::int64_t reportPeriodUs{1'000'000};  // How often the status line is printed.
    constexpr const char* helpText{
        "Commands: r = reset count, i = init, d = deinit, h = help\n"
        "Pin level is 1 with no magnet and 0 while a magnet is at the sensor.\n"};

    const driver::odometer::Config config{
        .pulsesPerRevolution = 2U,   // 2 magnets per wheel
        .wheelDiameterM = 0.031F,    // 31 mm wheel
    };

    driver::serial::Esp32s3 serial(driver::serial::Config{
        .port = UART_NUM_0,
        .txPin = UART_PIN_NO_CHANGE,
        .rxPin = UART_PIN_NO_CHANGE,
        .baudRate = 115200,
        .rxBufSize = 256U,
        .useUsbJtag = true,
    });
    serial.connect();

    char buf[160]{'\0'};

    driver::gpio::Esp32s3 odometerGpio(odometerPin, driver::gpio::Direction::InputPullup);
    driver::odometer::A3144 odometer(odometerGpio, config);

    std::snprintf(buf, sizeof(buf), "\nOdometer test: GPIO%u, %u pulses/rev, wheel %.1f mm, %.2f mm/pulse\n",
                  odometerPin, config.pulsesPerRevolution,
                  static_cast<double>(config.wheelDiameterM * 1000.0F),
                  static_cast<double>(driver::odometer::distancePerPulse(config) * 1000.0F));
    serial.write(buf);
    serial.write(odometerGpio.isInitialized() ? "GPIO init OK\n" : "GPIO init FAILED (pin invalid or taken)\n");
    serial.write(odometer.init() ? "Odometer init OK\n" : "Odometer init FAILED (interrupt setup)\n");
    serial.write(helpText);

    std::uint32_t lastCount{0U};
    std::int64_t lastPulseUs{esp_timer_get_time()};
    std::int64_t lastReportUs{lastPulseUs};
    float lastReportDistanceM{0.0F};

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(pollPeriodMs));
        const std::int64_t nowUs{esp_timer_get_time()};

        // Handle commands.
        if (serial.isDataAvailable())
        {
            char line[16]{'\0'};
            serial.read(line, sizeof(line));

            if (std::strcmp(line, "r") == 0)
            {
                odometer.reset();
                lastReportDistanceM = 0.0F;
                serial.write("Count reset\n");
            }
            else if (std::strcmp(line, "i") == 0)
            {
                serial.write(odometer.init() ? "Init OK\n" : "Init failed (already initialized?)\n");
            }
            else if (std::strcmp(line, "d") == 0)
            {
                serial.write(odometer.deinit() ? "Deinit OK, pulses are no longer counted\n"
                                               : "Deinit failed (not initialized?)\n");
            }
            else { serial.write(helpText); }

            lastCount = odometer.pulseCount();
        }

        // Print a line for every new pulse (several at once means the loop fell behind).
        const std::uint32_t count{odometer.pulseCount()};
        if (count != lastCount)
        {
            std::snprintf(buf, sizeof(buf), "Pulse #%lu (+%lu), %.1f ms since last, speed %.3f m/s\n",
                          static_cast<unsigned long>(count),
                          static_cast<unsigned long>(count - lastCount),
                          static_cast<double>(nowUs - lastPulseUs) / 1000.0,
                          static_cast<double>(odometer.speed()));
            serial.write(buf);
            lastCount   = count;
            lastPulseUs = nowUs;
        }

        // Print a status line every report period.
        if ((nowUs - lastReportUs) >= reportPeriodUs)
        {
            const float distanceM{odometer.distance()};
            const float windowS{static_cast<float>(nowUs - lastReportUs) * 1.0e-6F};
            const float averageSpeed{(distanceM - lastReportDistanceM) / windowS};

            std::snprintf(buf, sizeof(buf),
                          "[%s] pin %d, pulses %lu, distance %.3f m, speed %.3f m/s, avg last %.1f s %.3f m/s\n",
                          odometer.isInitialized() ? "ON " : "OFF",
                          odometerGpio.read() ? 1 : 0,
                          static_cast<unsigned long>(count),
                          static_cast<double>(distanceM),
                          static_cast<double>(odometer.speed()),
                          static_cast<double>(windowS),
                          static_cast<double>(averageSpeed));
            serial.write(buf);
            lastReportUs        = nowUs;
            lastReportDistanceM = distanceM;
        }
    }
    #else
    std::atomic<bool> stop{false};
    driver::factory::Esp32s3 factory;
    app::logic::Logic logic(factory);
    logic.run(stop);
    #endif

}
