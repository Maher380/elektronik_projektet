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
#include "driver/ir_sensor/stub.h"
#include "driver/motor/stub.h"
#include "driver/pwm/stub.h"
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
     * @brief Create a simulated PWM stub instance with default PWM settings.
     *
     * @param[in] pin The hardware pin number to simulate.
     * @return A unique pointer to the created simulated PWM interface instance.
     */
    std::unique_ptr<pwm::Interface> pwm(std::uint8_t pin) noexcept override {
        driver::pwm::Config config{};
        config.pin = pin;
        return std::make_unique<driver::pwm::Stub>(config);
    }

    /**
     * @brief Create a simulated PWM stub instance with an explicit configuration.
     *
     * @param[in] config Simulated PWM configuration.
     * @return A unique pointer to the created simulated PWM interface instance.
     */
    std::unique_ptr<pwm::Interface> pwm(const driver::pwm::Config& config) noexcept override {
        return std::make_unique<driver::pwm::Stub>(config);
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
     * @brief Create a simulated IR-Sensor stub instance.
     *
     * @param[in] adc Reference to the initialized ADC driver instance to use for reading.
     * @return A unique pointer ti the vreated simulated IR-Senor interface instance.
     */
    std::unique_ptr<ir_sensor::Interface> ir_sensor(adc::Interface&) noexcept override
    {
        return std::make_unique<driver::ir_sensor::Stub>();
    }

    /**
     * @brief Create a simulated motor stub instance.
     *
     * @param[in] motorForwardsPwm PWM driver used for the forward input.
     * @param[in] motorBackwardsPwm PWM driver used for the backward input.
     * @return A unique pointer to the created simulated motor interface instance.
     */
    std::unique_ptr<motor::Interface> motor(driver::pwm::Interface& motorForwardsPwm,
                                            driver::pwm::Interface& motorBackwardsPwm) noexcept override
    {
        (void)motorForwardsPwm;
        (void)motorBackwardsPwm;
        return std::make_unique<driver::motor::Stub>();
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
