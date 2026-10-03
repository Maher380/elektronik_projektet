/**
 * @file stub.h
 * @brief Voltage meter stub for simulation.
 */

#pragma once

#include "driver/voltage_meter/interface.h"

namespace driver::voltage_meter
{
/**
 * @brief Voltage meter stub implementation.
 * Used to test without hardware.
 */
class Stub final : public Interface
{
public:
    /**
     * @brief Constructor.
     */
    Stub() noexcept
        : myVoltage{7.4F}
        , myState{false}
    {}

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Read the simulated voltage.
     *
     * @return Voltage in Volts.
     */
    float readVoltage() noexcept override
    {
        return myVoltage;
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
     * @brief Simulate a voltage for testing purposes.
     *
     * @param[in] voltage The voltage to simulate (in Volts).
     */
    void setVoltage(const float voltage) noexcept
    {
        myVoltage = voltage;
    }

    /**
     * @brief Enable the stub.
     *
     * @param[in] state True to enable, false to disable.
     */
    void initMeter(const bool state) noexcept
    {
        myState = state;
    }

    Stub(const Stub&)            = delete; // No copy constructor.
    Stub& operator=(const Stub&) = delete; // No copy assignment.
    Stub(Stub&&)                 = delete; // No move constructor.
    Stub& operator=(Stub&&)      = delete; // No move assignment.

private:
    /** Simulated voltage in Volts. */
    float myVoltage;

    /** Simulated voltage meter state. */
    bool myState;
};
} // namespace driver::voltage_meter
