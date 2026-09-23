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
        , myEdge{Edge::Rising}
        , myCallback{nullptr}
        , myCallbackArg{nullptr}
    {}

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Write the simulated digital output state of the GPIO pin.
     * Calls the interrupt callback if the change matches the enabled edge.
     * * @param[in] state True to set the simulated pin logic high, false to set it logic low.
     */
    void write(bool state) noexcept override {
        const bool rising{!myState && state};
        const bool falling{myState && !state};
        myState = state;

        if (nullptr == myCallback) { return; }

        if ((rising && (Edge::Falling != myEdge)) || (falling && (Edge::Rising != myEdge)))
        {
            myCallback(myCallbackArg);
        }
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

    /**
     * @brief Call a function whenever the given edge is simulated via write().
     *
     * @param[in] edge The edge that triggers the callback.
     * @param[in] callback Function to call on each matching edge.
     * @param[in] arg User argument passed to the callback.
     *
     * @return True if the interrupt was enabled, false if the callback is null
     *         or an interrupt is already enabled.
     */
    bool enableInterrupt(Edge edge, InterruptCallback callback, void* arg) noexcept override {
        if ((nullptr != myCallback) || (nullptr == callback)) { return false; }
        myEdge        = edge;
        myCallback    = callback;
        myCallbackArg = arg;
        return true;
    }

    /**
     * @brief Stop calling the interrupt callback.
     */
    void disableInterrupt() noexcept override {
        myCallback    = nullptr;
        myCallbackArg = nullptr;
    }

    // Delete copy/move operators.
    Stub(const Stub&)            = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(const Stub&) = delete;
    Stub& operator=(Stub&&)      = delete;

private:
    /** @brief The simulated state of the GPIO pin (true = logic high, false = logic low). */
    bool myState;
    /** @brief The edge that triggers the interrupt callback. */
    Edge myEdge;
    /** @brief The interrupt callback, nullptr if no interrupt is enabled. */
    InterruptCallback myCallback;
    /** @brief User argument passed to the interrupt callback. */
    void* myCallbackArg;
};
} // namespace driver::gpio
