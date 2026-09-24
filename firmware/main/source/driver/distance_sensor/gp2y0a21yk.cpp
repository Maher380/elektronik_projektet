

#include "driver/distance_sensor/gp2y0a21yk.h"
#include <cmath>
#include <limits>


namespace
{
    constexpr float scalingConstant = 28.153f;

    constexpr float exponent = -1.175f;

} // namespace

namespace driver::distance_sensor
{


    GP2Y0A21YK::GP2Y0A21YK(driver::adc::Interface& adc) noexcept
    : myAdc{adc}
    {}

   
    float GP2Y0A21YK::readDistance() noexcept 
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

        bool GP2Y0A21YK::isInitialized() const noexcept 
    {
        return myAdc.isInitialized();
    }

} // namespace driver::distance_sensor
