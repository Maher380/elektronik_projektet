#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "driver/adc/stub.h"
#include "driver/factory/stub.h"
#include "driver/temperature_sensor/tmp36.h"
#include "test/temperature_sensor.h"

namespace
{
bool expect(bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("Temperature sensor test failed: %s\n", message);
        return false;
    }

    return true;
}

// Temperature for a raw count on the ADC stub, which reads raw / 4095 * 3.3 V.
float temperatureFor(std::uint16_t raw) noexcept
{
    return ((static_cast<float>(raw) / 4095.0F) * 3.3F - 0.5F) * 100.0F;
}

bool isNear(float value, float expected) noexcept
{
    return std::fabs(value - expected) < 0.01F;
}
} // namespace

namespace test
{
bool runTemperatureSensorTest() noexcept
{
    using driver::temperature_sensor::Tmp36;

    driver::adc::Stub adc;
    Tmp36 sensor{adc};
    bool passed = expect(!sensor.isInitialized(), "sensor should not be ready before its ADC")
        && expect(std::isnan(sensor.readTemperature()), "uninitialized sensor should read NaN")
        && expect(adc.init() && sensor.isInitialized(), "sensor should be ready with its ADC");

    // 931 counts is 0.75 V, about 25 °C; 1241 counts is 1.0 V, about 50 °C.
    const float room = temperatureFor(931U);
    const float warm = temperatureFor(1241U);
    passed = passed && expect((std::fabs(room - 25.0F) < 0.1F) && (std::fabs(warm - 50.0F) < 0.1F),
                              "test points should be about 25 and 50 °C");

    // The first sample fills the whole buffer.
    adc.simulateInput(931U);
    passed = passed && expect(isNear(sensor.readTemperature(), room), "first sample should be the average");

    // One new sample replaces one of SampleCount in the average.
    constexpr float Count{static_cast<float>(Tmp36::SampleCount)};
    adc.simulateInput(1241U);
    passed = passed
        && expect(isNear(sensor.readTemperature(), ((Count - 1.0F) * room + warm) / Count),
                  "second sample should move the average by one part in SampleCount");

    for (std::size_t i{1U}; i < Tmp36::SampleCount; ++i) { sensor.readTemperature(); }
    passed = passed && expect(isNear(sensor.readTemperature(), warm), "SampleCount new samples should replace the old ones");

    // A disconnected (0 V, −50 °C) or shorted (3.3 V, 280 °C) sensor reads NaN and leaves the average alone.
    adc.simulateInput(0U);
    passed = passed && expect(std::isnan(sensor.readTemperature()), "reading below −40 °C should be NaN");
    adc.simulateInput(4095U);
    passed = passed && expect(std::isnan(sensor.readTemperature()), "reading above 125 °C should be NaN");
    adc.simulateInput(1241U);
    passed = passed && expect(isNear(sensor.readTemperature(), warm), "out-of-range reads should not enter the average");

    // A failed read reports NaN.
    passed = passed && expect(adc.deinit() && std::isnan(sensor.readTemperature()), "failed read should be NaN");

    driver::factory::Stub factory;
    driver::adc::Stub factoryAdc;
    passed = passed && expect(factory.temperatureSensor(factoryAdc) != nullptr,
                              "factory should create a temperature sensor");

    return passed;
}
} // namespace test
