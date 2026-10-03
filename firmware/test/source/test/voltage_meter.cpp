#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>

#include "driver/adc/stub.h"
#include "driver/voltage_meter/divider.h"
#include "test/voltage_meter.h"

namespace
{
bool expect(bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("Voltage meter test failed: %s\n", message);
        return false;
    }

    return true;
}

// R1 = 30 kΩ and R2 = 10 kΩ make the measured voltage 4 times the pin voltage.
constexpr float R1Ohm{30000.0F};
constexpr float R2Ohm{10000.0F};
constexpr float Scale{4.0F};

// Measured voltage for a raw count on the ADC stub, which reads raw / 4095 * 3.3 V.
float measuredFor(std::uint16_t raw) noexcept
{
    return (static_cast<float>(raw) / 4095.0F) * 3.3F * Scale;
}

bool isNear(float value, float expected) noexcept
{
    return std::fabs(value - expected) < 0.001F;
}

bool hasInvalidResistors(float r1Ohm, float r2Ohm) noexcept
{
    driver::adc::Stub adc;
    driver::voltage_meter::Divider meter{adc, r1Ohm, r2Ohm};
    return adc.init() && !meter.isInitialized() && std::isnan(meter.readVoltage());
}
} // namespace

namespace test
{
bool runVoltageMeterTest() noexcept
{
    constexpr float nan{std::numeric_limits<float>::quiet_NaN()};
    bool passed = expect(hasInvalidResistors(R1Ohm, 0.0F), "R2 of 0 should be rejected")
        && expect(hasInvalidResistors(-1.0F, R2Ohm), "negative R1 should be rejected")
        && expect(hasInvalidResistors(nan, R2Ohm), "NaN R1 should be rejected")
        && expect(hasInvalidResistors(R1Ohm, nan), "NaN R2 should be rejected");

    driver::adc::Stub adc;
    driver::voltage_meter::Divider meter{adc, R1Ohm, R2Ohm};
    passed = passed
        && expect(!meter.isInitialized(), "meter should not be ready before its ADC")
        && expect(std::isnan(meter.readVoltage()), "uninitialized meter should read NaN")
        && expect(adc.init() && meter.isInitialized(), "meter should be ready with its ADC");

    // The first sample fills the whole buffer.
    const float low = measuredFor(2048U);
    const float high = measuredFor(4095U);
    adc.simulateInput(2048U);
    passed = passed && expect(isNear(meter.readVoltage(), low), "first sample should be the average");

    // One new sample replaces one of SampleCount in the average.
    constexpr float Count{static_cast<float>(driver::voltage_meter::Divider::SampleCount)};
    adc.simulateInput(4095U);
    passed = passed
        && expect(isNear(meter.readVoltage(), ((Count - 1.0F) * low + high) / Count),
                  "second sample should move the average by one part in SampleCount");

    for (std::size_t i{1U}; i < driver::voltage_meter::Divider::SampleCount; ++i) { meter.readVoltage(); }
    passed = passed && expect(isNear(meter.readVoltage(), high), "SampleCount new samples should replace the old ones");

    // A failed read reports NaN and leaves the average alone.
    adc.simulateInput(2048U);
    passed = passed
        && expect(adc.deinit() && std::isnan(meter.readVoltage()), "failed read should be NaN")
        && expect(adc.init(), "ADC should initialize again")
        && expect(isNear(meter.readVoltage(), ((Count - 1.0F) * high + low) / Count),
                  "failed read should not enter the average");

    return passed;
}
} // namespace test
