#include "driver/voltage_meter/divider.h"

#include <cmath>
#include <limits>

namespace driver::voltage_meter
{
namespace
{
/**
 * @brief Measured voltage per ADC pin voltage.
 *
 * @return (R1 + R2) / R2, or NaN if the resistor values are invalid.
 */
float scaleFor(float r1Ohm, float r2Ohm) noexcept
{
    if (!std::isfinite(r1Ohm) || !std::isfinite(r2Ohm) || (r1Ohm < 0.0F) || (r2Ohm <= 0.0F))
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return (r1Ohm + r2Ohm) / r2Ohm;
}
} // namespace

Divider::Divider(driver::adc::Interface& adc, float r1Ohm, float r2Ohm) noexcept
    : myAdc{adc}
    , myScale{scaleFor(r1Ohm, r2Ohm)}
    , mySamples{}
    , myNext{0U}
    , mySeeded{false}
{}

float Divider::readVoltage() noexcept
{
    constexpr float invalid{std::numeric_limits<float>::quiet_NaN()};
    if (!isInitialized()) { return invalid; }

    const float pinVoltage = myAdc.readVoltage();
    if (!std::isfinite(pinVoltage) || (pinVoltage < 0.0F)) { return invalid; }

    const float voltage = pinVoltage * myScale;

    // Fill the whole buffer with the first sample, so the average is right from the start.
    if (!mySeeded)
    {
        mySamples.fill(voltage);
        mySeeded = true;
    }
    mySamples[myNext] = voltage;
    myNext = (myNext + 1U) % SampleCount;

    // Sum the whole buffer each time: cheap for 16 samples, and a float running sum would drift.
    float sum{0.0F};
    for (const float sample : mySamples) { sum += sample; }
    return sum / static_cast<float>(SampleCount);
}

bool Divider::isInitialized() const noexcept
{
    return myAdc.isInitialized() && std::isfinite(myScale);
}
} // namespace driver::voltage_meter
