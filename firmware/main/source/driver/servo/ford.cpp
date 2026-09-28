/**
 * @file ford.cpp
 * @brief Ford servo driver implementation.
 */

#include "driver/servo/ford.h"

#include <algorithm>

#include "driver/pwm/interface.h"

namespace driver::servo
{

Ford::Ford(pwm::Interface& pwm) noexcept
    : myPwm{pwm}
{}

bool Ford::init() noexcept
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

bool Ford::deinit() noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    myIsInitialized = false;
    return myPwm.deinit();
}

bool Ford::isInitialized() const noexcept
{
    return myIsInitialized;
}

bool Ford::setDirection(const float angleDegrees) noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    // Each side is scaled on its own so 0 degrees always gives the center pulse.
    const float clampedDegrees = std::clamp(angleDegrees, MinAngleDegrees, MaxAngleDegrees);
    const float pulseUs = clampedDegrees <= 0.0F
        ? CenterPulseUs + (clampedDegrees / MinAngleDegrees) * (LeftPulseUs - CenterPulseUs)
        : CenterPulseUs + (clampedDegrees / MaxAngleDegrees) * (RightPulseUs - CenterPulseUs);
    const float duty = pulseUs * static_cast<float>(myPwm.frequencyHz()) / 1'000'000.0F;
    if (!myPwm.setDuty(duty))
    {
        return false;
    }

    myDirection = clampedDegrees;
    return true;
}

float Ford::getDirection() const noexcept
{
    return myDirection;
}

bool Ford::center() noexcept
{
    return setDirection(0.0F);
}

} // namespace driver::servo
