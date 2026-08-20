/**
 * @file interface.h
 * @brief Interface for Timer driver.
 */

#pragma once
#include <cstdint>


namespace driver::timer {

/**
 * @brief Interface for imer driver.
 */

class Interface {
public:

    /**
     * @brief Virtual destructor.
     */
    virtual ~Interface() noexcept = default; 
    
    /**
     * @brief Starts the timer.
     */
    virtual void start() noexcept = 0;

    /**
     * @brief Stops the timer.
     */
    virtual void stop() noexcept = 0;
    
    /**
     * @brief Sets the timer period in milliseconds.
     * * @param[in] period_ms The alarm period in milliseconds.
     */
    virtual void setPeriod(std::uint32_t period_ms) noexcept = 0;
    
    /**
     * @brief Check if a timeout has occured since the last call.
     * @return True if a timeout occurred, false otherwise.
     */
    virtual bool isTimeout() noexcept = 0; 

    /**
     * @brief Checks if the timer hardware is successfully initialized.
     * * @return True if initialized and ready for use, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;
};

} // namespace driver::timer
