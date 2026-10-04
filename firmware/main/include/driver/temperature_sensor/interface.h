/**
 * @file interface.h
 * @brief Temperature sensor interface.
 */

#pragma once

namespace driver::temperature_sensor
{
/**
 * @brief Temperature sensor interface.
 */
class Interface
{
public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Read the measured temperature.
     *
     * @return Temperature in degrees Celsius, or NaN if no valid reading is available.
     */
    virtual float readTemperature() noexcept = 0;

    /**
     * @brief Check if the temperature sensor is ready to read.
     *
     * @return True if ready, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;
};
} // namespace driver::temperature_sensor
