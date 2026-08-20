/**
 * @file esp32s3.h
 * @brief Real hardware interface of the Pin Manager for ESP32-S3.
 */

#pragma once

#include <cstdint>

#include "system/pin_manager/interface.h"

namespace sys::pin_manager
{
/**
 * @brief Pin manager interface for ESP32-S3.
 * Manages resource allocation and tracking for GPIO pins using a thread-safe Singleton.
 * This class cannot be copied or moved.
 */	
class Esp32s3 final : public Interface
{
public:
     /**
     * @brief Return singleton ESP32-S3 pin manager instance.
     * @return Reference to the active pin manager interface instance.
     */ 
    static Interface& instance() noexcept;

     /**
     * @brief Check if pin is valid.
     * @param[in] pin Corresponding bit to check in PinRegister.
     * @return True if valid, otherwise false.
     */ 
    bool isPinValid(std::uint8_t pin) const noexcept override;

     /**
     * @brief Check if pin is being used.
     * @param[in] pin Corresponding bit to check in PinRegister.
     * @return True if free, false if busy or invalid.
     */
    bool isPinBusy(std::uint8_t pin) const noexcept override;

     /**
     * @brief Reserve pin.
     * @param[in] pin Corresponding bit to reserve in PinRegister.
     * @return True on success, false otherwise.  
     */
    bool reservePin(std::uint8_t pin) noexcept override;

     /**
     * @brief Release pin.
     * @param[in] pin Corresponding bit to release in PinRegister.
     */
    void releasePin(std::uint8_t pin) noexcept override;


    // Delete no copy/move constructors and operators.
    Esp32s3(const Esp32s3&)            = delete;
    Esp32s3(Esp32s3&&)                 = delete;
    Esp32s3& operator=(const Esp32s3&) = delete;
    Esp32s3& operator=(Esp32s3&&)      = delete;


private:
    /**
     * @brief Private constructor to prevent direct instantiation (Singleton).
     */
    Esp32s3() noexcept;

    /**
     * @brief Destructor.
     */
    ~Esp32s3() noexcept override = default;
    
    /** @brief 64-bit register tracking the allocation state of individual GPIO pins. */
    std::uint64_t myPinReg;
};
} // namespace sys::pin_manager
