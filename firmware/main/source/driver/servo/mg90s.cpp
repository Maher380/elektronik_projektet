/**
 * @file mg90s.cpp
 * @brief MG90S servo driver implementation.
 */

#include "driver/servo/mg90s.h"

#include <algorithm>

#include "driver/pwm/interface.h"

namespace driver::servo
{

Mg90s::Mg90s(pwm::Interface& pwm) noexcept
    : myPwm{pwm}
{}

bool Mg90s::init() noexcept
{
    if (myIsInitialized)
    {
        return false;
    }

    const std::uint32_t frequencyHz = myPwm.frequencyHz();
    if (frequencyHz < MinFrequencyHz || frequencyHz > MaxFrequencyHz)
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

bool Mg90s::deinit() noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    myIsInitialized = false;
    return myPwm.deinit();
}

bool Mg90s::isInitialized() const noexcept
{
    return myIsInitialized;
}

bool Mg90s::setDirection(const float angleDegrees) noexcept
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
    const float safePulseUs = std::clamp(pulseUs, MinPulseUs, MaxPulseUs);
    const float duty = safePulseUs * static_cast<float>(myPwm.frequencyHz()) / 1'000'000.0F;
    if (!myPwm.setDuty(duty))
    {
        return false;
    }

    myDirection = clampedDegrees;
    return true;
}

float Mg90s::getDirection() const noexcept
{
    return myDirection;
}

bool Mg90s::center() noexcept
{
    return setDirection(0.0F);
}

} // namespace driver::servo
