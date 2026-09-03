#include "driver/factory/esp32s3.h"

#include "driver/adc/esp32s3.h"
#include "driver/ir_sensor/esp32s3.h"
#include "driver/gpio/esp32s3.h"
#include "driver/motor/l298n.h"
#include "driver/motor/mp6550.h"
#include "driver/pwm/esp32s3.h"
#include "driver/servo/esp32s3.h"
#include "driver/serial/esp32s3.h"
#include "driver/timer/esp32s3.h"
#include "driver/wifi/esp32s3.h"

namespace driver::factory {


std::unique_ptr<adc::Interface> Esp32s3::adc(std::uint8_t pin) noexcept {
    // Instante the real ADC driver and encapsulate the pin at construction
    return std::make_unique<driver::adc::Esp32s3>(pin);
}

std::unique_ptr < ir_sensor::Interface> Esp32s3::ir_sensor(adc::Interface& adc) noexcept
{
    return std::make_unique<driver::ir_sensor::Esp32s3>(adc);
}

std::unique_ptr<gpio::Interface> Esp32s3::gpioInput(std::uint8_t pin) noexcept {
    // Create a real GPIO pin configured as an input
    return std::make_unique<driver::gpio::Esp32s3>(pin, driver::gpio::Direction::Input);
}


std::unique_ptr<gpio::Interface> Esp32s3::gpioOutput(std::uint8_t pin) noexcept {
    // Create a real GPIO pin configured as an output
    return std::make_unique<driver::gpio::Esp32s3>(pin, driver::gpio::Direction::Output);
}

std::unique_ptr<pwm::Interface> Esp32s3::pwm(std::uint8_t pin) noexcept {
    // Create a real PWM output using default PWM settings.
    return std::make_unique<driver::pwm::Esp32s3>(pin);
}

std::unique_ptr<pwm::Interface> Esp32s3::pwm(const driver::pwm::Config& config) noexcept {
    // Create a real PWM output using an explicit PWM configuration.
    return std::make_unique<driver::pwm::Esp32s3>(config);
}

std::unique_ptr<servo::Interface> Esp32s3::servo(driver::pwm::Interface& pwm) noexcept {
    return std::make_unique<driver::servo::Esp32s3>(pwm);
}

std::unique_ptr<motor::Interface> Esp32s3::motor(driver::pwm::Interface& MotorForwardsPwm,
                                                 driver::pwm::Interface& MotorBackwardsPwm) noexcept {
    // Create a real MP6550 motor driver from existing PWM outputs.
    return std::make_unique<driver::motor::MP6550>(MotorForwardsPwm, MotorBackwardsPwm);
}

std::unique_ptr<serial::Interface> Esp32s3::serial(std::uint32_t baud_bps) noexcept {

    const driver::serial::Config config{
        .port = UART_NUM_0,
        .txPin = UART_PIN_NO_CHANGE,
        .rxPin = UART_PIN_NO_CHANGE,
        .baudRate = static_cast<int>(baud_bps),
        .rxBufSize = 256U,
        .useUsbJtag = true,
    };

    return std::make_unique<driver::serial::Esp32s3>(config);
}

std::unique_ptr<timer::Interface> Esp32s3::timer(std::uint32_t timeout_ms) noexcept {

    auto t = std::make_unique<driver::timer::Esp32s3>();
    t->setPeriod(timeout_ms);
    return t;
}

std::unique_ptr<wifi::Interface> Esp32s3::wifi(const char* ssid, const char* password) noexcept {

    return std::make_unique<driver::wifi::Esp32s3>(ssid, password);
}

} // namespace driver::factory
