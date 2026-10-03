/**
 * @file interface.h
 * @brief Voltage meter interface.
 */

#pragma once

namespace driver::voltage_meter
{
/**
 * @brief Voltage meter interface.
 *
 * Measures a voltage that is too high for the ADC pin, such as a battery.
 */
class Interface
{
public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Read the measured voltage.
     *
     * @return Voltage in Volts, or NaN if no valid reading is available.
     */
    virtual float readVoltage() noexcept = 0;

    /**
     * @brief Check if the voltage meter is ready to read.
     *
     * @return True if ready, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;
};
} // namespace driver::voltage_meter
