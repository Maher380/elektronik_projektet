/**
 * @file interface.h
 * @brief Abstract interface for the system Pin Manager module.
 */

#pragma once

#include <cstdint>

namespace sys::pin_manager
{

/**
 * @brief Abstract interface for GPIO pin management.
 * Provides an interface for validating, checking, reserving, and releasing
 * hardware pins to prevent resource conflicts.
 */	
class Interface
{
public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Check if pin is valid for the target hardware.
     * @param[in] pin Corresponding bit to check in PinRegister.
     * @return True if valid, false otherwise.
     */
    virtual bool isPinValid(std::uint8_t pin) const noexcept = 0;

    /**
     * @brief Check if pin is being used.
     * @param[in] pin Corresponding bit to check in PinRegister.
     * @return True if free, false if busy or inavlid.
     */
    virtual bool isPinBusy(std::uint8_t pin) const noexcept = 0;

    /**
     * @brief Reserve pin.
     * @param[in] pin Corresponding bit to reserve in PinRegister
     * @return True on success, false otherwise.  
     */
    virtual bool reservePin(std::uint8_t pin) noexcept = 0;

    /**
     * @brief Release pin.
     * @param[in] pin Corresponding bit to release in PinRegister
     */
    virtual void releasePin(std::uint8_t pin) noexcept = 0;
};
} // namespace sys::pin_manager
