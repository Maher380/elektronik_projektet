/**
 * @file stub.h
 * @brief Temperature sensor stub for simulation.
 */

#pragma once

#include "driver/temperature_sensor/interface.h"

namespace driver::temperature_sensor
{
/**
 * @brief Temperature sensor stub implementation.
 * Used to test without hardware.
 */
class Stub final : public Interface
{
public:
    /**
     * @brief Constructor.
     */
    Stub() noexcept
        : myTemperature{25.0F}
        , myState{false}
    {}

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Read the simulated temperature.
     *
     * @return Temperature in degrees Celsius.
     */
    float readTemperature() noexcept override
    {
        return myTemperature;
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
     * @brief Simulate a temperature for testing purposes.
     *
     * @param[in] temperature The temperature to simulate (in degrees Celsius).
     */
    void setTemperature(const float temperature) noexcept
    {
        myTemperature = temperature;
    }

    /**
     * @brief Enable the stub.
     *
     * @param[in] state True to enable, false to disable.
     */
    void initSensor(const bool state) noexcept
    {
        myState = state;
    }

    Stub(const Stub&)            = delete; // No copy constructor.
    Stub& operator=(const Stub&) = delete; // No copy assignment.
    Stub(Stub&&)                 = delete; // No move constructor.
    Stub& operator=(Stub&&)      = delete; // No move assignment.

private:
    /** Simulated temperature in degrees Celsius. */
    float myTemperature;

    /** Simulated temperature sensor state. */
    bool myState;
};
} // namespace driver::temperature_sensor
