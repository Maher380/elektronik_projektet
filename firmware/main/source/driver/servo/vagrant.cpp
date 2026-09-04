/**
 * @file vagrant.cpp
 * @brief Vagrant servo driver implementation.
 */

#include "driver/servo/vagrant.h"

#include "driver/pwm/interface.h"

namespace driver::servo
{

Vagrant::Vagrant(pwm::Interface& pwm) noexcept
    : myPwm{pwm}
{}

bool Vagrant::init() noexcept
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

bool Vagrant::deinit() noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    myIsInitialized = false;
    return myPwm.deinit();
}

bool Vagrant::isInitialized() const noexcept
{
    return myIsInitialized;
}

bool Vagrant::setDirection(const float angleDegrees) noexcept
{
    if (!myIsInitialized || angleDegrees < MinAngleDegrees || angleDegrees > MaxAngleDegrees)
    {
        return false;
    }

    const std::uint32_t frequency = angleDegrees <= 0.0F
        ? MinFrequencyHz + static_cast<std::uint32_t>((angleDegrees - MinAngleDegrees) *
                                                       (CenterFrequencyHz - MinFrequencyHz) /
                                                       (0.0F - MinAngleDegrees))
        : CenterFrequencyHz + static_cast<std::uint32_t>(angleDegrees *
                                                          (MaxFrequencyHz - CenterFrequencyHz) /
                                                          MaxAngleDegrees);
    if (!myPwm.setFrequencyHz(frequency))
    {
        return false;
    }

    myDirection = angleDegrees;
    return true;
}

float Vagrant::getDirection() const noexcept
{
    return myDirection;
}

bool Vagrant::center() noexcept
{
    return setDirection(0.0F);
}

} // namespace driver::servo