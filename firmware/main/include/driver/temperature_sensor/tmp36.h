/**
 * @file tmp36.h
 * @brief TMP36 analog temperature sensor.
 */

#pragma once

#include <array>
#include <cstddef>

#include "driver/adc/interface.h"
#include "driver/temperature_sensor/interface.h"

namespace driver::temperature_sensor
{
/**
 * @brief TMP36 analog temperature sensor.
 *
 * @verbatim
   2.7–5.5 V ──── +Vs
                   TMP36  Vout ──► ADC pin
   GND ────────── GND
   @endverbatim
 *
 * T = (Vout − 0.5 V) × 100 °C/V: 0.5 V at 0 °C, 10 mV/°C, specified from
 * −40 °C (0.1 V) to +125 °C (1.75 V).
 *
 * Each readTemperature() takes one ADC sample and returns the average of the
 * last SampleCount samples. The first valid sample fills the whole buffer.
 * A sample outside the specified range is treated as a wiring fault, not
 * added to the average.
 *
 * @note The average covers the last SampleCount calls, not a fixed time, so
 *       call readTemperature() at a steady rate.
 *
 * Platform independent: works with any ADC driver.
 */
class Tmp36 final : public Interface
{
public:
    /** Number of samples in the moving average. */
    static constexpr std::size_t SampleCount{16U};
    /** Lowest temperature the TMP36 is specified for, in degrees Celsius. */
    static constexpr float MinTemperatureC{-40.0F};
    /** Highest temperature the TMP36 is specified for, in degrees Celsius. */
    static constexpr float MaxTemperatureC{125.0F};

    /**
     * @brief Constructor.
     *
     * @param[in] adc Reference to an ADC driver connected to the TMP36 output.
     */
    explicit Tmp36(driver::adc::Interface& adc) noexcept;

    /**
     * @brief Destructor.
     */
    ~Tmp36() noexcept override = default;

    /**
     * @brief Take one sample and read the averaged temperature.
     *
     * @return Average of the last SampleCount samples in degrees Celsius, or
     *         NaN if uninitialized or if this sample failed or is out of range.
     *         A failed sample is not added to the average.
     */
    float readTemperature() noexcept override;

    /**
     * @brief Check if the ADC is initialized.
     *
     * @return True if ready, false otherwise.
     */
    bool isInitialized() const noexcept override;

    Tmp36(const Tmp36&)            = delete; // No copy constructor.
    Tmp36(Tmp36&&)                 = delete; // No move constructor.
    Tmp36& operator=(const Tmp36&) = delete; // No copy assignment.
    Tmp36& operator=(Tmp36&&)      = delete; // No move assignment.

private:
    /** ADC connected to the TMP36 output. */
    driver::adc::Interface& myAdc;
    /** Last SampleCount temperatures, in degrees Celsius. */
    std::array<float, SampleCount> mySamples;
    /** Index of the oldest sample, which the next sample replaces. */
    std::size_t myNext;
    /** True once the first valid sample has filled the buffer. */
    bool mySeeded;
};
} // namespace driver::temperature_sensor
