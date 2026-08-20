/*
 * @file interface.h 
 * @brief Abstract interface for GPIO driver.
 */

#pragma once

#include <cstdint>

namespace driver::gpio
{
/**
 * @brief Abstract interface for GPIO operations.
 */
class Interface
{
public:
    /**
     * @brief Virtual destructor to ensure proper cleanup of derived objects.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Write the digital output state of the GPIO pin.
     * * @param[in] state True to set the pin logic high, false to set it logic low.
     */
    virtual void write(bool state) noexcept = 0;

    /**
     * @brief Read the digital input state of the GPIO pin.
     * * @return True if the pin is logic high, false if it is logic low.
     */
    virtual bool read() const noexcept = 0;

    /**
     * @brief Toggle the current digital output state of the GPIO pin.
     */
    virtual void toggle() noexcept = 0;

    /**
     * @brief Check if the GPIO pin has been successfully configured and initialized.
     * * @return True if the driver is initialized and ready for use, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;
    
};
} // namespace driver::gpio
