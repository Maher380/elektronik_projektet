/**
 * @file IR sensor interface.
 */

#pragma once 


namespace driver::ir_sensor
{

class Interface
{

public:
    
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Read the current distance from the sensor.
     * 
     * @return Distance in cm, or NaN if no valid reading is available.
     */
    virtual float readDistance() noexcept = 0;

    /**
     * @brief Check if sensor is running.
     * 
     * @return True if enabled, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;

};
} // namespace driver::ir_sensor
