/**
 * @file start_module_test.cpp
 * @brief Start module test app, run when START_MODULE_TEST_MODE is defined in main.cpp.
 *
 * @note Remove this file (and the START_MODULE_TEST_MODE define in main.cpp) once no longer needed.
 */

#include "test_app/test_app.h"

#include <cstdint>
#include <cstdio>

#include "driver/gpio/esp32s3.h"
#include "driver/serial/esp32s3.h"
#include "driver/start_module/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app::test_app
{
namespace
{
const char* toString(const driver::start_module::State state) noexcept
{
    switch (state)
    {
        case driver::start_module::State::Waiting: return "WAITING";
        case driver::start_module::State::Started: return "STARTED";
        case driver::start_module::State::Stopped: return "STOPPED";
    }
    return "UNKNOWN";
}
} // namespace

void runStartModuleTest() noexcept
{
    constexpr std::uint8_t startModulePin{8U};    // ~D5 / GPIO8
    constexpr std::uint32_t pollPeriodMs{1000U};  // How often the start module is polled and reported.

    driver::serial::Esp32s3 serial(driver::serial::Config{
        .port = UART_NUM_0,
        .txPin = UART_PIN_NO_CHANGE,
        .rxPin = UART_PIN_NO_CHANGE,
        .baudRate = 115200,
        .rxBufSize = 256U,
        .useUsbJtag = true,
    });
    serial.connect();

    char buf[128]{'\0'};

    driver::gpio::Esp32s3 startModuleGpio(startModulePin, driver::gpio::Direction::Input);
    driver::start_module::Gpio startModule(startModuleGpio);

    std::snprintf(buf, sizeof(buf), "\nStart module test: GPIO%u, hold time %lu ms, polled every %lu ms\n",
                  startModulePin,
                  static_cast<unsigned long>(driver::start_module::Gpio::DefaultHoldTimeMs),
                  static_cast<unsigned long>(pollPeriodMs));
    serial.write(buf);
    serial.write(startModule.isInitialized() ? "GPIO init OK\n" : "GPIO init FAILED (pin invalid or taken)\n");
    serial.write("Start needs a low first and counts on the second high poll; the first low after it stops.\n"
                 "Stopped is final: restart to reset.\n");

    while (true)
    {
        const auto nowMs{static_cast<std::uint32_t>(esp_timer_get_time() / 1000)};
        startModule.update(nowMs);

        std::snprintf(buf, sizeof(buf), "[%lu ms] pin %d, %s\n",
                      static_cast<unsigned long>(nowMs),
                      startModuleGpio.read() ? 1 : 0,
                      toString(startModule.state()));
        serial.write(buf);

        vTaskDelay(pdMS_TO_TICKS(pollPeriodMs));
    }
}

} // namespace app::test_app
