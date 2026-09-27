/*
 * @file interface.h 
 * @brief Abstract interface for GPIO driver.
 */

#pragma once

#include <cstdint>

#include "driver/gpio/edge.h"

namespace driver::gpio
{
/**
 * @brief Interrupt callback type.
 *
 * @param[in] arg User argument passed to enableInterrupt().
 *
 * @attention On hardware the callback runs in interrupt context and must be ISR-safe.
 */
using InterruptCallback = void (*)(void* arg);

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

    /**
     * @brief Call a function whenever the given edge occurs on the GPIO pin.
     *
     * @param[in] edge The edge that triggers the interrupt.
     * @param[in] callback Function to call on each interrupt.
     * @param[in] arg User argument passed to the callback.
     *
     * @return True if the interrupt was enabled, false otherwise.
     */
    virtual bool enableInterrupt(Edge edge, InterruptCallback callback, void* arg) noexcept = 0;

    /**
     * @brief Stop calling the interrupt callback. Does nothing if no interrupt is enabled.
     */
    virtual void disableInterrupt() noexcept = 0;

};
} // namespace driver::gpio
