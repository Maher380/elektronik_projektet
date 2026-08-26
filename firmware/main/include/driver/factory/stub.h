/**
 * @file stub.h
 * @brief Factory interface for creating driver stubs in simulation.
 */

#pragma once

#include "driver/factory/interface.h"
#include <memory>
#include <cstdint>

#include "driver/adc/stub.h"
#include "driver/gpio/stub.h"
#include "driver/serial/stub.h"
#include "driver/timer/stub.h"
#include "driver/wifi/stub.h"

namespace driver::factory {

/**
 * @brief Factory for creating simulated driver stubs.
 * Used to run and test the application without physical hardware.
 */

class Stub final : public Interface {
public:
    /**
     * @brief Constructor.
     */
    Stub() noexcept = default;

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Create a simulated GPIO input stub instance.
     *
     * @param[in] pin The hardware pin number to simulate (unused).
     * @return A unique pointer to the created simulated GPIO interface instance.
     */
    std::unique_ptr<gpio::Interface> gpioInput(std::uint8_t pin) noexcept override {
        (void)pin;
        return std::make_unique<driver::gpio::Stub>(); 
    }

    /**
     * @brief Create a simulated GPIO output stub instance.
     *
     * @param[in] pin The hardware pin number to simulate (unused).
     * @return A unique pointer to the created simulated GPIO interface instance.
     */
    std::unique_ptr<gpio::Interface> gpioOutput(std::uint8_t pin) noexcept override {
        (void)pin;
        return std::make_unique<driver::gpio::Stub>(); 
    }

    /**
     * @brief Create a simulated ADC stub instance.
     *
     * @param[in] pin The hardware pin number to simulate (unused).
     * @return A unique pointer to the created simulated ADC interface instance.
     */
    std::unique_ptr<adc::Interface> adc(std::uint8_t pin) noexcept override {
        (void)pin;
	return std::make_unique<driver::adc::Stub>();
    }

    /**
     * @brief Create a simulated Serial stub instance.
     *
     * @param[in] baud_bps Serial baud rate to simulate (unused).
     * @return A unique pointer to the created simulated Serial interface instance.
     */
    std::unique_ptr<serial::Interface> serial(std::uint32_t baud_bps) noexcept override {
        (void)baud_bps;
        return std::make_unique<driver::serial::Stub>();
    }

    /**
     * @brief Create a simulated Timer stub instance.
     *
     * @param[in] timeout_ms The timer timeout duration to simulate (unused).
     * @return A unique pointer to the created simulated Timer interface instance.
     */
    std::unique_ptr<timer::Interface> timer(std::uint32_t timeout_ms) noexcept override {
        (void)timeout_ms;
        return std::make_unique<driver::timer::Stub>(); 
    }

    /**
     * @brief Create a simulated WiFi stub instance.
     *
     * @param[in] ssid WiFi network SSID to simulate (unused).
     * @param[in] password WiFi network password to simulate (unused).
     * @return A unique pointer to the created simulated WiFi stub instance.
     */
    std::unique_ptr<wifi::Interface> wifi(const char* ssid, const char* password) noexcept override {
        (void)ssid;
        (void)password;
        return std::make_unique<driver::wifi::Stub>();
    }

    // No copy and move
    Stub(const Stub&)            = delete;
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(Stub&&)      = delete;
};

} // namespace driver::factory
