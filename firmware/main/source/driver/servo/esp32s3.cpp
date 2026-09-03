/**
 * @file esp32s3.cpp
 * @brief ESP32-S3 servo driver skeleton.
 */

#include "driver/servo/esp32s3.h"

#include "driver/pwm/interface.h"

namespace driver::servo
{
Esp32s3::Esp32s3(pwm::Interface& pwm) noexcept
    : myPwm{pwm}
{}

bool Esp32s3::init() noexcept
{
    if (myIsInitialized)
    {
        return false;
    }

    if (!myPwm.isInitialized() && !myPwm.init())
    {
        return false;
    }

    myIsInitialized = true;
    return center();
}

bool Esp32s3::deinit() noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    myIsInitialized = false;
    return myPwm.deinit();
}

bool Esp32s3::isInitialized() const noexcept
{
    return myIsInitialized;
}

bool Esp32s3::setDirection(const float angleDegrees) noexcept
{
    if (!myIsInitialized || angleDegrees < MinAngleDegrees || angleDegrees > MaxAngleDegrees)
    {
        return false;
    }

    const float normalizedAngle = (angleDegrees - MinAngleDegrees) /
                                  (MaxAngleDegrees - MinAngleDegrees);
    const float duty = MinDuty + (normalizedAngle * (MaxDuty - MinDuty));
    if (!myPwm.setDuty(duty))
    {
        return false;
    }

    myDirection = angleDegrees;
    return true;
}

float Esp32s3::getDirection() const noexcept
{
    return myDirection;
}

bool Esp32s3::center() noexcept
{
    return setDirection(0.0F);
}

} // namespace driver::servo
