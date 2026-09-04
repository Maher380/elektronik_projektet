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

    if (!myPwm.setDuty(0.5F))
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

    // The physical servo is mounted with the steering direction reversed.
    // Invert the requested angle before converting it to PWM frequency so the
    // logical left/right commands match the actual steering direction.
    const float mirroredAngleDegrees = -angleDegrees;
    const std::uint32_t frequency = mirroredAngleDegrees <= 0.0F
        ? MinFrequencyHz + static_cast<std::uint32_t>((mirroredAngleDegrees - MinAngleDegrees) *
                                                       (CenterFrequencyHz - MinFrequencyHz) /
                                                       (0.0F - MinAngleDegrees))
        : CenterFrequencyHz + static_cast<std::uint32_t>(mirroredAngleDegrees *
                                                          (MaxFrequencyHz - CenterFrequencyHz) /
                                                          MaxAngleDegrees);
    if (myPwm.frequencyHz() != frequency && !myPwm.setFrequencyHz(frequency))
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