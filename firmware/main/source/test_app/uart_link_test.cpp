/**
 * @file uart_link_test.cpp
 * @brief Pi UART link test app, run when UART_LINK_TEST_MODE is defined in main.cpp.
 *
 * Echoes every line received from the Raspberry Pi on UART1 back to it, and reports on
 * the USB console what arrived. The Pi end is a script that sends lines and checks each
 * echo, so this app does nothing on its own initiative on the UART.
 *
 * @note Remove this file (and the UART_LINK_TEST_MODE define in main.cpp) once the Pi
 *       link has its real protocol.
 */

#include "test_app/test_app.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "driver/gpio.h"
#include "driver/gpio/esp32s3.h"
#include "driver/serial/esp32s3.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system/ford.h"

namespace app::test_app
{
namespace
{
constexpr uart_port_t PiPort{UART_NUM_1};

void printHelp(driver::serial::Esp32s3& console) noexcept
{
    console.write("Commands (type + Enter):\n"
                  "  b <baud>  change the Pi link baud rate, e.g. b 921600\n"
                  "  s <text>  send one line to the Pi\n"
                  "  h         this help\n");
}
} // namespace

void runUartLinkTest() noexcept
{
    constexpr int startBaud{115200};
    constexpr std::uint32_t reportPeriodMs{1000U};

    // Hold the motor still before anything else. This app never drives, but an undriven
    // BRAKE and SPD left the car driving on 2026-10-07 when it ran on the drive battery.
    driver::gpio::Esp32s3 brake(app::ford::pin::Brake, driver::gpio::Direction::Output);
    driver::gpio::Esp32s3 speed(app::ford::pin::Speed, driver::gpio::Direction::Output);
    brake.write(true);
    speed.write(false);

    driver::serial::Esp32s3 console(driver::serial::Config{
        .port = UART_NUM_0,
        .txPin = UART_PIN_NO_CHANGE,
        .rxPin = UART_PIN_NO_CHANGE,
        .baudRate = 115200,
        .rxBufSize = 256U,
        .useUsbJtag = true,
    });
    console.connect();

    // The receive buffer must be larger than the 128-byte hardware FIFO.
    driver::serial::Esp32s3 pi(driver::serial::Config{
        .port = PiPort,
        .txPin = app::ford::pin::PiUartTx,
        .rxPin = app::ford::pin::PiUartRx,
        .baudRate = startBaud,
        .rxBufSize = 1024U,
    });
    const bool piOk{pi.connect()};
    // A powered-off Pi leaves RX floating; the pull-up makes that read as an idle line, not noise.
    gpio_pullup_en(static_cast<gpio_num_t>(app::ford::pin::PiUartRx));

    char buf[256]{'\0'};
    std::snprintf(buf, sizeof(buf),
                  "\nPi UART link test: UART1, TX GPIO%u (D7), RX GPIO%u (D8), %d baud\n",
                  app::ford::pin::PiUartTx, app::ford::pin::PiUartRx, startBaud);
    console.write(buf);
    console.write(piOk ? "UART1 init OK\n" : "UART1 init FAILED\n");
    printHelp(console);

    std::uint32_t lines{0U};
    std::uint32_t bytes{0U};
    std::uint32_t linesAtLastReport{0U};
    std::uint32_t lastReportMs{0U};
    char line[256]{'\0'};
    char last[48]{'\0'};

    while (true)
    {
        while (pi.isDataAvailable())
        {
            const std::uint16_t n{pi.read(line, sizeof(line))};
            pi.write(line);
            pi.write('\n');
            ++lines;
            bytes += n + 1U;
            std::snprintf(last, sizeof(last), "%.40s", line); // The start is enough to recognise it.
        }

        if (console.isDataAvailable())
        {
            char cmd[96]{'\0'};
            console.read(cmd, sizeof(cmd));
            if ((cmd[0] == 'b') && (cmd[1] == ' '))
            {
                const long baud{std::strtol(cmd + 2, nullptr, 10)};
                const bool ok{(baud > 0) && (uart_set_baudrate(PiPort, static_cast<std::uint32_t>(baud)) == ESP_OK)};
                std::snprintf(buf, sizeof(buf), ok ? "Baud now %ld\n" : "Baud %ld refused\n", baud);
                console.write(buf);
            }
            else if ((cmd[0] == 's') && (cmd[1] == ' '))
            {
                pi.write(cmd + 2);
                pi.write('\n');
                console.write("Sent\n");
            }
            else
            {
                printHelp(console);
            }
        }

        const auto nowMs{static_cast<std::uint32_t>(esp_timer_get_time() / 1000)};
        if ((nowMs - lastReportMs) >= reportPeriodMs)
        {
            lastReportMs = nowMs;
            if (lines != linesAtLastReport)
            {
                std::snprintf(buf, sizeof(buf), "[%lu ms] echoed %lu lines, %lu bytes; last \"%s\"\n",
                              static_cast<unsigned long>(nowMs), static_cast<unsigned long>(lines),
                              static_cast<unsigned long>(bytes), last);
                console.write(buf);
                linesAtLastReport = lines;
            }
        }

        // One tick, never zero: at a 100 Hz tick rate pdMS_TO_TICKS(1) is 0, which would
        // starve the idle task and trip the task watchdog. The Pi waits for each echo, so
        // the line-end queue never fills in the meantime.
        vTaskDelay(1);
    }
}

} // namespace app::test_app
