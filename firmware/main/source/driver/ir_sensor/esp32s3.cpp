

#include "driver/ir_sensor/esp32s3.h"
#include <cmath>
#include <limits>


namespace
{
    constexpr float scalingConstant = 28.153f;

    constexpr float exponent = -1.175f;

} // namespace

namespace driver::ir_sensor
{


    Esp32s3::Esp32s3(driver::adc::Interface& adc) noexcept
    : myAdc{adc}
    {}

   
    float Esp32s3::readDistance() noexcept 
    {
        if (!myAdc.isInitialized())
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
        
        const float voltage = myAdc.readVoltage();

        if (!std::isfinite(voltage) || (voltage <= 0.0F))
        {
            return std::numeric_limits<float>::quiet_NaN();
        }

        const float distance = (scalingConstant * std::pow(voltage, exponent));

        return std::isfinite(distance)
            ? distance
            : std::numeric_limits<float>::quiet_NaN();
    }

        bool Esp32s3::isInitialized() const noexcept 
    {
        return myAdc.isInitialized();
    }

} // namespace driver::ir_sensor
