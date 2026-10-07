/**
 * @file stub.h
 * @brief Start module stub for simulation.
 */

#pragma once

#include <cstdint>

#include "driver/start_module/interface.h"

namespace driver::start_module
{
/**
 * @brief Start module stub implementation.
 * Used to test without hardware.
 */
class Stub final : public Interface
{
public:
    /**
     * @brief Constructor.
     */
    Stub() noexcept
        : myStarted{false}
        , myState{true}
    {}

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Does nothing; use setStarted() to simulate the start signal.
     *
     * @param[in] nowMs Current time in milliseconds (unused).
     */
    void update(const std::uint32_t nowMs) noexcept override
    {
        (void)nowMs;
    }

    /**
     * @brief Check whether the simulated start signal has come.
     *
     * @return True if started, false otherwise.
     */
    bool isStarted() const noexcept override
    {
        return myStarted;
    }

    /**
     * @brief Check if the stub is enabled.
     *
     * @return True if enabled, false otherwise.
     */
    bool isInitialized() const noexcept override
    {
        return myState;
    }

    /**
     * @brief Simulate the start signal for testing purposes.
     *
     * @param[in] started True to simulate a start, false to simulate waiting.
     */
    void setStarted(const bool started) noexcept
    {
        myStarted = started;
    }

    /**
     * @brief Enable the stub.
     *
     * @param[in] state True to enable, false to disable.
     */
    void initModule(const bool state) noexcept
    {
        myState = state;
    }

    Stub(const Stub&)            = delete; // No copy constructor.
    Stub& operator=(const Stub&) = delete; // No copy assignment.
    Stub(Stub&&)                 = delete; // No move constructor.
    Stub& operator=(Stub&&)      = delete; // No move assignment.

private:
    /** Simulated start signal. */
    bool myStarted;
    /** Whether the stub reports itself as initialized. */
    bool myState;
};
} // namespace driver::start_module
