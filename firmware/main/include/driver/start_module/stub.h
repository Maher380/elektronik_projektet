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
        : myModuleState{State::Waiting}
        , myState{true}
    {}

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Does nothing; use setState() to simulate the start and stop signals.
     *
     * @param[in] nowMs Current time in milliseconds (unused).
     */
    void update(const std::uint32_t nowMs) noexcept override
    {
        (void)nowMs;
    }

    /**
     * @brief Get the simulated state of the start module.
     *
     * @return The state set by setState(), Waiting by default.
     */
    State state() const noexcept override
    {
        return myModuleState;
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
     * @brief Simulate the start module state for testing purposes.
     *
     * @param[in] state The state to simulate.
     */
    void setState(const State state) noexcept
    {
        myModuleState = state;
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
    /** Simulated start module state. */
    State myModuleState;
    /** Whether the stub reports itself as initialized. */
    bool myState;
};
} // namespace driver::start_module
