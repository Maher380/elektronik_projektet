#include "driver/temperature_sensor/tmp36.h"

#include <cmath>
#include <limits>

namespace driver::temperature_sensor
{
namespace
{
/** TMP36 output at 0 °C, in Volts. */
constexpr float OffsetVolts{0.5F};
/** TMP36 slope, 10 mV/°C. */
constexpr float DegreesPerVolt{100.0F};
} // namespace

Tmp36::Tmp36(driver::adc::Interface& adc) noexcept
    : myAdc{adc}
    , mySamples{}
    , myNext{0U}
    , mySeeded{false}
{}

float Tmp36::readTemperature() noexcept
{
    constexpr float invalid{std::numeric_limits<float>::quiet_NaN()};
    if (!isInitialized()) { return invalid; }

    const float pinVoltage = myAdc.readVoltage();
    if (!std::isfinite(pinVoltage)) { return invalid; }

    // Out of range means a loose or shorted sensor, not a real temperature.
    const float temperature = (pinVoltage - OffsetVolts) * DegreesPerVolt;
    if ((temperature < MinTemperatureC) || (temperature > MaxTemperatureC)) { return invalid; }

    // Fill the whole buffer with the first sample, so the average is right from the start.
    if (!mySeeded)
    {
        mySamples.fill(temperature);
        mySeeded = true;
    }
    mySamples[myNext] = temperature;
    myNext = (myNext + 1U) % SampleCount;

    float sum{0.0F};
    for (const float sample : mySamples) { sum += sample; }
    return sum / static_cast<float>(SampleCount);
}

bool Tmp36::isInitialized() const noexcept
{
    return myAdc.isInitialized();
}
} // namespace driver::temperature_sensor
