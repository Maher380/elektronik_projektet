/**
 * @file srf05_test.cpp
 * @brief SRF05-only test app, run when SRF05_TEST_MODE is defined in main.cpp.
 *
 * @note Remove this file (and the SRF05_TEST_MODE define in main.cpp) once no longer needed.
 */

#include "test_app/test_app.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "driver/distance_sensor/srf05.h"
#include "driver/gpio/esp32s3.h"
#include "driver/serial/esp32s3.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app::test_app
{

void runSrf05Test() noexcept
{
    constexpr std::uint8_t triggerPin{17U};            // D8 / GPIO17
    constexpr std::uint8_t echoPin{10U};               // D7 / GPIO10, via 5 V -> 3.3 V divider
    constexpr std::uint32_t pollPeriodMs{10U};         // How often the sensor is read.
    constexpr std::int64_t reportPeriodUs{200'000};    // How often the status line is printed.
    constexpr std::int64_t timingPeriodUs{10'000'000}; // How often the readDistance() timing is printed.

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

    driver::gpio::Esp32s3 triggerGpio(triggerPin, driver::gpio::Direction::Output);
    driver::gpio::Esp32s3 echoGpio(echoPin, driver::gpio::Direction::Input);
    driver::distance_sensor::SRF05 sensor(triggerGpio, echoGpio);

    std::snprintf(buf, sizeof(buf), "\nSRF05 test: trigger GPIO%u, echo GPIO%u\n", triggerPin, echoPin);
    serial.write(buf);
    serial.write(triggerGpio.isInitialized() ? "Trigger GPIO init OK\n" : "Trigger GPIO init FAILED (pin invalid or taken)\n");
    serial.write(echoGpio.isInitialized() ? "Echo GPIO init OK\n" : "Echo GPIO init FAILED (pin invalid or taken)\n");
    serial.write(sensor.isInitialized() ? "SRF05 init OK\n" : "SRF05 init FAILED (GPIO or interrupt setup)\n");

    std::int64_t lastReportUs{esp_timer_get_time()};
    std::uint32_t reads{0U};
    std::uint32_t validReads{0U};
    float lastValidCm{0.0F};

    // readDistance() execution time statistics (Welford's algorithm), reset every timing period.
    std::int64_t lastTimingUs{lastReportUs};
    std::uint32_t timingCount{0U};
    double timingMeanUs{0.0};
    double timingM2{0.0};

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(pollPeriodMs));
        const std::int64_t nowUs{esp_timer_get_time()};

        const std::int64_t readStartUs{esp_timer_get_time()};
        const float distanceCm{sensor.readDistance()};
        const double readDurationUs{static_cast<double>(esp_timer_get_time() - readStartUs)};

        ++timingCount;
        const double delta{readDurationUs - timingMeanUs};
        timingMeanUs += delta / timingCount;
        timingM2     += delta * (readDurationUs - timingMeanUs);

        ++reads;
        if (std::isfinite(distanceCm))
        {
            ++validReads;
            lastValidCm = distanceCm;
        }

        // Print a status line every report period.
        if ((nowUs - lastReportUs) >= reportPeriodUs)
        {
            std::snprintf(buf, sizeof(buf), "distance %6.1f cm (last valid %6.1f cm), valid %lu/%lu, echo pin %d\n",
                          static_cast<double>(distanceCm),
                          static_cast<double>(lastValidCm),
                          static_cast<unsigned long>(validReads),
                          static_cast<unsigned long>(reads),
                          echoGpio.read() ? 1 : 0);
            serial.write(buf);
            lastReportUs = nowUs;
            reads        = 0U;
            validReads   = 0U;
        }

        // Print the readDistance() timing every timing period.
        if ((nowUs - lastTimingUs) >= timingPeriodUs)
        {
            const double stdDevUs{(timingCount > 1U) ? std::sqrt(timingM2 / (timingCount - 1U)) : 0.0};
            std::snprintf(buf, sizeof(buf), "readDistance() time: avg %.2f us, std dev %.2f us, n = %lu\n",
                          timingMeanUs, stdDevUs, static_cast<unsigned long>(timingCount));
            serial.write(buf);
            lastTimingUs = nowUs;
            timingCount  = 0U;
            timingMeanUs = 0.0;
            timingM2     = 0.0;
        }
    }
}

} // namespace app::test_app
