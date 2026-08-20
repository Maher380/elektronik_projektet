/*
 * @file stub.h
 * @brief GPIO driver stub.
 */
#pragma once

#include <cstdint>
#include "driver/gpio/interface.h"

namespace driver::gpio
{
/**
 * @brief GPIO driver stub for simulation.
 * * This class simulates physical GPIO behavior in memory.
 * It is non-copyable and non-movable.
 */
class Stub final : public Interface
{
public:
    /**
     * @brief Constructor.
     * Initializes the simulated pin state to logic low.
     */
    Stub() noexcept
        : myState{false}
    {}

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Write the simulated digital output state of the GPIO pin.
     * * @param[in] state True to set the simulated pin logic high, false to set it logic low.
     */ 
    void write(bool state) noexcept override {
        myState = state;
    }

     /**
     * @brief Read the simulated digital state of the GPIO pin.
     * * @return True if the simulated pin is logic high, false if it is logic low.
     */ 
    bool read() const noexcept override {
        return myState;
    }

     /**
     * @brief Toggle the current simulated digital state of the GPIO pin.
     */ 
    void toggle() noexcept override {
        myState = !myState;
    }

     /**
     * @brief Check if the simulated GPIO driver is initialized.
     * * @return Always returns true since the simulation stub requires no hardware setup.
     */ 
    bool isInitialized() const noexcept override {
    return true;
    }
    // Delete copy/move operators.
    Stub(const Stub&)            = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(const Stub&) = delete;
    Stub& operator=(Stub&&)      = delete;

private:
    /** @brief The simulated state of the GPIO pin (true = logic high, false = logic low). */
    bool myState;
};
} // namespace driver::gpio
