/**
 * @file esp32s3.h
 * @brief GPIO driver for ESP32-S3.
 */
#pragma once

#include <cstdint>

#include "driver/gpio/direction.h"
#include "driver/gpio/interface.h"

namespace driver::gpio
{
/**
 * @brief GPIO driver implementation for ESP32-S3.
 * * This class handles physical GPIO pin configuration.
 * It is explicitly non-copyable and non-movable.
 */
class Esp32s3 final : public Interface
{
public:
    /**
     * @brief Constructor.
     * 
     * @param[in] pin GPIO pin. Must be supported by ESP32-S3.
     * @param[in] direction Data direction.
     */
    explicit Esp32s3(std::uint8_t pin, Direction direction) noexcept;

    /**
     * @brief Destructor.
     */
    ~Esp32s3() noexcept override;

    /**
     * @brief Write GPIO state.
     * 
     * @param[in] True if the pin is logic high, false if it is logic low.
     */
    void write(bool state) noexcept override;

    /**
     * @brief Read GPIO state.
     * 
     * @return True if the GPIO is active/high, false otherwise.
     */
    bool read() const noexcept override;

    /**
     * @brief Toggle the GPIO.
     */
    void toggle() noexcept override;

    /**
     * @brief Check if the GPIO pin was successfully initialized.
     * * @return True if the hardware configuration succeeded, false otherwise.
     */
    bool isInitialized() const noexcept override;

    // Delete default constructor, copy/move constructors and assignment operators
    Esp32s3()                          = delete;
    Esp32s3(const Esp32s3&)            = delete; 
    Esp32s3(Esp32s3&&)                 = delete; 
    Esp32s3& operator=(const Esp32s3&) = delete; 
    Esp32s3& operator=(Esp32s3&&)      = delete; 

private:
    /** @brief The assigned hardware GPIO pin number. */
    const std::uint8_t myPin;
    /** @brief The configured data direction for the pin. */
    const Direction myDirection;
    /** @brief Flag indicating if the hardware was successfully initialized. */
    bool myInitialized;
};
} // namespace driver::gpio
