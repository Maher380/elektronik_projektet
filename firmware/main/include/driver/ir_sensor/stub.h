/**
 * @file IR sensor stub.
 */

#pragma once 

#include "driver/ir_sensor/interface.h"


namespace driver::ir_sensor
{

class Stub final : public Interface
{

public:
    
    /**
     * @brief Constructor.
     */
    Stub() noexcept
    : myDistance{10.0f}
    , myState{false}
    {}


    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Read the current distance from the sensor.
     * 
     * @return Distance in cm
     */
    float readDistance() noexcept override 
    {
        return myDistance;
    }

    /**
     * @brief Check if sensor is running.
     * 
     * @return True if enabled, false otherwise.
     */
    bool isInitialized() const noexcept override 
    {
        return myState;
    }

    /**
     * @brief Simulate distance for testing purposes.
     * 
     * @param[in] distance The distance to simulate (in cm).
     */
    void setDistance(const float distance ) noexcept 
    {
        myDistance = distance;
    }

    /**
     * @brief Enable mock sensor.
     * 
     * @param[in] state initiate IR-sensor, true to enable, false to disable.
     */
    void initSensor(const bool state) noexcept
    {

        myState = state;
    }

    // Delete copy/move cpnstructors and operators.
    Stub(const Stub&)            = delete;
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(Stub&&)      = delete;

private:

    /** Simulated distance value. */
    float myDistance;

    /** Simulated IR-sensor state. */
    bool myState;
};
} // namespace driver::ir_sensor
