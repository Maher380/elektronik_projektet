/** 
 * @file stub.h
 * @brief Timer driver stub for simulation.
 */

#pragma once
#include "interface.h"


namespace driver::timer {

/**
 * @brief Timer driver stub for simulation.
 * This class is used for unit testing system logic without requiring 
 * actual ESP32-S3 hardware.
 * This class cannot be copied or moved.
 */
class Stub : public Interface {
public:
    /**
     * @brief Constructor. Initializes the stub variables with default values.
     */
    Stub() : my_running{false}, my_timeout{false}, my_period{0} {}

    // Delete no/copy move constructors and operators.
    Stub(const Stub&)            = delete; 
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&)                 = delete; 
    Stub& operator=(Stub&&)      = delete;

    /**
     * @brief Destructor.
     */
    virtual ~Stub() noexcept override = default;

    /** @brief Simulate starting the timer. */
    void start() noexcept override {
	    my_running = true;
    }

    /** @brief Simulate stopping the timer. */
    void stop() noexcept override {
	    my_running = false;
    }

    /** @brief Set the simulated timer period in milliseconds.
     * @param[in] period_ms Period in milliseconds.
     */
    void setPeriod(std::uint32_t period_ms) noexcept override {
	    my_period = period_ms;
    }

    /** @brief Returns the simulated timeout state and resets it.
     * @return True if a timeout has occured, false otherwise.
     */
    bool isTimeout() noexcept override {
        bool result = my_timeout;
        my_timeout = false;
        return result;
    }

    /** @brief Check the initialization status of the stub.
     * @return True since stubs are always considered initialized.
     */
    bool isInitialized() const noexcept override {
	    return true;
    }
    
    /**
     * @brief Manually triggers a timeout for testing purposes.
     */
    void simulateTimeout() noexcept {
	    my_timeout = true;
    }

    /**
     * @brief Check if the timer is currently running.
     * @return True of running, false otherwise.
     */
    bool isRunning() const noexcept {
	    return my_running;
    }

    /**
     * @brief Get the current period set in the stub.
     * @return The configured period in milliseconds.
     */
    std::uint32_t getPeriod() const noexcept {
	    return my_period;
    }

private:
    /** @brief Flag tracking if the simulated timer is running. */
    bool my_running;

    /** @brief Flag tracking if a simulated timeout has occurred. */
    bool my_timeout;

    /** @brief The currently configured simulated period in milliseconds. */
    std::uint32_t my_period;
};

} // namespace driver::timer
