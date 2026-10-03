/**
 * @file divider.h
 * @brief Voltage meter that reads through a resistor voltage divider.
 */

#pragma once

#include <array>
#include <cstddef>

#include "driver/adc/interface.h"
#include "driver/voltage_meter/interface.h"

namespace driver::voltage_meter
{
/**
 * @brief Voltage meter that reads through a resistor voltage divider.
 *
 * Wiring: measured voltage -> R1 -> ADC pin -> R2 -> GND. A capacitor from the
 * ADC pin to GND (about 100 nF) is needed for an accurate ADC reading through
 * resistors this large. It does not change the conversion, so it is not a
 * parameter.
 *
 * Each readVoltage() takes one ADC sample and returns the average of the last
 * SampleCount samples. The first valid sample fills the whole buffer.
 *
 * @note The average covers the last SampleCount calls, not a fixed time, so
 *       call readVoltage() at a steady rate. This could be improved by keeping
 *       the driver stateless and moving the averaging to a separate moving
 *       average filter that the caller feeds at a fixed rate.
 *
 * Platform independent: works with any ADC driver.
 */
class Divider final : public Interface
{
public:
    /** Number of samples in the moving average. */
    static constexpr std::size_t SampleCount{16U};

    /**
     * @brief Constructor.
     *
     * @param[in] adc Reference to an ADC driver connected to the divider joint.
     * @param[in] r1Ohm Resistor from the measured voltage to the ADC pin, in Ohms.
     * @param[in] r2Ohm Resistor from the ADC pin to GND, in Ohms.
     *
     * @note Invalid resistor values (r1Ohm < 0, r2Ohm <= 0 or not finite)
     *       leave the meter uninitialized.
     */
    Divider(driver::adc::Interface& adc, float r1Ohm, float r2Ohm) noexcept;

    /**
     * @brief Destructor.
     */
    ~Divider() noexcept override = default;

    /**
     * @brief Take one sample and read the averaged voltage.
     *
     * @return Average of the last SampleCount samples in Volts, or NaN if
     *         uninitialized or if this sample failed. A failed sample is not
     *         added to the average.
     */
    float readVoltage() noexcept override;

    /**
     * @brief Check if the ADC is initialized and the resistor values are valid.
     *
     * @return True if ready, false otherwise.
     */
    bool isInitialized() const noexcept override;

    Divider(const Divider&)            = delete; // No copy constructor.
    Divider(Divider&&)                 = delete; // No move constructor.
    Divider& operator=(const Divider&) = delete; // No copy assignment.
    Divider& operator=(Divider&&)      = delete; // No move assignment.

private:
    /** ADC connected to the divider joint. */
    driver::adc::Interface& myAdc;
    /** Measured voltage per ADC pin voltage, (R1 + R2) / R2. NaN if the resistors are invalid. */
    float myScale;
    /** Last SampleCount measured voltages, in Volts. */
    std::array<float, SampleCount> mySamples;
    /** Index of the oldest sample, which the next sample replaces. */
    std::size_t myNext;
    /** True once the first valid sample has filled the buffer. */
    bool mySeeded;
};
} // namespace driver::voltage_meter
