/**
 * @file interface.h
 * @brief Abstract interface for creating driver instances.
 */

#pragma once

#include <cstdint>
#include <memory>

namespace driver::adc { class Interface; }
namespace driver::gpio { class Interface; }
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
