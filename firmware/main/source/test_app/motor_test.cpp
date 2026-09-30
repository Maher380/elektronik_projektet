/**
 * @file motor_test.cpp
 * @brief A89301 motor test app with serial speed control, run when MOTOR_TEST_MODE is defined in main.cpp.
 *
 * @note Remove this file (and the MOTOR_TEST_MODE define in main.cpp) once no longer needed.
 */

#include "test_app/test_app.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "driver/gpio/esp32s3.h"
#include "driver/motor/a89301.h"
#include "driver/pwm/esp32s3.h"
#include "driver/serial/esp32s3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app::test_app
{

void runMotorTest() noexcept
{
    // A89301 motor test app. Speed and direction are set with serial commands (type + Enter):
    // a number 0.0 - 1.0 = speed, f = forward, b = backward, s = stop (coast), x = stop (brake),
    // h = help. Requires SPD to be configured for PWM mode in the A89301 EEPROM (it is on ford).
    // Wiring, speed range and safety: documentation/design_documents/ford_a89301_motor_controller.md
    //
    // @attention Unlike A89301_CONFIG_MODE, this app has no motor temperature, stall or speed limit
    //            protection. Keep runs short and watch the motor.
    constexpr std::uint8_t speedPin{8U};      // D5 / GPIO8 -> SPD
    constexpr std::uint8_t directionPin{7U};  // D4 / GPIO7 -> DIR
    constexpr std::uint8_t brakePin{5U};      // D2 / GPIO5 -> BRAKE
    constexpr std::uint32_t pollPeriodMs{10U};
    constexpr const char* helpText{
        "Commands: <0.0 - 1.0> = set speed, f = forward, b = backward,\n"
        "          s = stop (coast), x = stop (brake), h = help\n"};

    driver::serial::Esp32s3 serial(driver::serial::Config{
        .port = UART_NUM_0,
        .txPin = UART_PIN_NO_CHANGE,
        .rxPin = UART_PIN_NO_CHANGE,
        .baudRate = 115200,
        .rxBufSize = 256U,
        .useUsbJtag = true,
    });
    serial.connect();

    driver::pwm::Esp32s3 speedPwm(speedPin);
    driver::gpio::Esp32s3 directionGpio(directionPin, driver::gpio::Direction::Output);
    driver::gpio::Esp32s3 brakeGpio(brakePin, driver::gpio::Direction::Output);
    driver::motor::A89301 motor(speedPwm, directionGpio, brakeGpio);

    serial.write("\nA89301 motor test: SPD GPIO8 (D5), DIR GPIO7 (D4), BRAKE GPIO5 (D2)\n");
    if (!motor.init())
    {
        serial.write("A89301 init FAILED (pin invalid or taken?)\n");
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    serial.write("A89301 init OK, motor stopped\n");
    serial.write(helpText);

    char buf[96]{'\0'};
    float speed{0.0F};
    bool forward{true};

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(pollPeriodMs));
        if (!serial.isDataAvailable()) { continue; }

        char line[16]{'\0'};
        if (serial.read(line, sizeof(line)) == 0U) { continue; }

        char* end{nullptr};
        const float value{std::strtof(line, &end)};

        if ((end != line) && (*end == '\0'))
        {
            if (!motor.setSpeed(value))
            {
                serial.write("Speed must be in range 0.0 - 1.0\n");
                continue;
            }
            speed = value;
        }
        else if (std::strcmp(line, "f") == 0)
        {
            forward = true;
            motor.setDirection(driver::motor::Direction::Forward);
        }
        else if (std::strcmp(line, "b") == 0)
        {
            forward = false;
            motor.setDirection(driver::motor::Direction::Backward);
        }
        else if (std::strcmp(line, "s") == 0)
        {
            speed = 0.0F;
            motor.stop(driver::motor::StopMode::Coast);
        }
        else if (std::strcmp(line, "x") == 0)
        {
            speed = 0.0F;
            motor.stop(driver::motor::StopMode::Brake);
        }
        else
        {
            serial.write(helpText);
            continue;
        }

        std::snprintf(buf, sizeof(buf), "Speed %.2f (duty %.0f %%), direction %s, brake %s\n",
                      static_cast<double>(speed), static_cast<double>(speedPwm.duty() * 100.0F),
                      forward ? "forward" : "backward", brakeGpio.read() ? "ON" : "off");
        serial.write(buf);
    }
}

} // namespace app::test_app
