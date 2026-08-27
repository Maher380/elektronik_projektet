/**
 * @file interface.h
 * @brief Abstract interface for creating driver instances.
 */

#pragma once

#include <cstdint>
#include <memory>

namespace driver::ir_sensor {class Interface;}
namespace driver::adc { class Interface; }
namespace driver::gpio { class Interface; }
namespace driver::motor { class Interface; }
namespace driver::pwm { struct Config; class Interface; }
namespace driver::serial { class Interface; }
namespace driver::timer { class Interface; }
namespace driver::wifi { class Interface; }

namespace driver::factory {

/**
 * @brief Abstract Factory interface for creating drivers.
 */
class Interface {


public:
    virtual ~Interface() noexcept = default;

    /**
     * @brief Create an ADC driver instance.
     *
     * @param[in] pin The hardware pin number to use for the ADC channel.
     * @return A unique pointer to the created ADC interface instance.
     */
    virtual std::unique_ptr<adc::Interface> adc(std::uint8_t pin) noexcept = 0;

    /**
     * @brief Create a GPIO input driver instance.
     *
     * @param[in] pin The hardware pin number to configure as input.
     * @return A unique pointer to the created GPIO interface instance.
     */
    virtual std::unique_ptr<gpio::Interface> gpioInput(std::uint8_t pin) noexcept = 0;

    /**
     * @brief Create a GPIO output driver instance.
     *
     * @param[in] pin The hardware pin number to configure as output.
     * @return A unique pointer to the created GPIO interface instance.
     */
    virtual std::unique_ptr<gpio::Interface> gpioOutput(std::uint8_t pin) noexcept = 0;

    /**
     * @brief Create a PWM driver instance with default PWM settings.
     *
     * @param[in] pin The hardware pin number to configure as PWM output.
     * @return A unique pointer to the created PWM interface instance.
     */
    virtual std::unique_ptr<pwm::Interface> pwm(std::uint8_t pin) noexcept = 0;

    /**
     * @brief Create a PWM driver instance with an explicit configuration.
     *
     * @param[in] config PWM output configuration.
     * @return A unique pointer to the created PWM interface instance.
     */
    virtual std::unique_ptr<pwm::Interface> pwm(const pwm::Config& config) noexcept = 0;

    /**
     * @brief Create an Ir sensor driver instance.
     *
     * @param[in] adc Reference to an initialized ADC driver instance used for reading.
     * @return A unique pointer to the created Ir-sensor interface instance.
     */
    virtual std::unique_ptr<ir_sensor::Interface> ir_sensor(adc::Interface&) noexcept = 0;

    /**
     * @brief Create a motor driver instance.
     *
     * @param[in] MotorForwardsPwm PWM output driver used for IN1.
     * @param[in] MotorBackwardsPwm PWM output driver used for IN2.
     * @return A unique pointer to the created motor interface instance.
     */
    virtual std::unique_ptr<motor::Interface> motor(driver::pwm::Interface& MotorForwardsPwm,
                                                    driver::pwm::Interface& MotorBackwardsPwm) noexcept = 0;


    /**
     * @brief Create a serial driver instance.
     *
     * @param[in] baud_bps Serial baud rate in bits per second.
     * @return A unique pointer to the created Serial interface instance.
     */
    virtual std::unique_ptr<serial::Interface> serial(std::uint32_t baud_bps) noexcept = 0;

    /**
     * @brief Create a Timer driver instance.
     *
     * @param[in] timeout_ms The timer timeout duration specified in milliseconds.
     * @return A unique pointer to the created Timer interface instance.
     */
    virtual std::unique_ptr<timer::Interface> timer(std::uint32_t timeout_ms) noexcept = 0;

    /**
     * @brief Create a WiFi driver instance.
     *
     * @param[in] ssid WiFi network SSID.
     * @param[in] password WiFi network password.
     * @return A unique pointer to the created WiFi interface instance.
     */
    virtual std::unique_ptr<wifi::Interface> wifi(const char* ssid, const char* password) noexcept = 0;

    // No copy and move operations allowed
    Interface(const Interface&)            = delete;
    Interface& operator=(const Interface&) = delete;
    Interface(Interface&&)                 = delete;
    Interface& operator=(Interface&&)      = delete;


protected:

    /**
     * @brief Protected constructor to allow inheritance but prevent creating objects directly.
     */

    Interface() noexcept = default;
};

} // namespace driver::factory
