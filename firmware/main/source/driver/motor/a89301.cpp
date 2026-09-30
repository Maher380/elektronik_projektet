/**
 * @file a89301.cpp
 * @brief A89301 sensorless BLDC motor controller implementation.
 */

#include "driver/motor/a89301.h"

namespace driver::motor
{
namespace
{
/**
 * @brief Check if a normalized speed value is valid.
 *
 * @param[in] speed Speed value to validate.
 * @return True if speed is in range 0.0f - 1.0f.
 */
bool isSpeedValid(const float speed) noexcept
{
    return (speed >= 0.0F) && (speed <= 1.0F);
}
} // namespace

A89301::A89301(driver::pwm::Interface& speedPwm,
               driver::gpio::Interface& direction,
               driver::gpio::Interface& brake,
               const bool invertDirection) noexcept
    : mySpeedPwm{speedPwm}
    , myDirectionPin{direction}
    , myBrakePin{brake}
    , myInvertDirection{invertDirection}
    , myIsInitialized{false}
    , myPwmInitializedByDriver{false}
    , myDirection{Direction::Forward}
    , mySpeed{0.0F}
{}

bool A89301::init() noexcept
{
    if (myIsInitialized || !myDirectionPin.isInitialized() || !myBrakePin.isInitialized())
    {
        return false;
    }

    if (!mySpeedPwm.isInitialized())
    {
        if (!mySpeedPwm.init())
        {
            return false;
        }

        myPwmInitializedByDriver = true;
    }

    if (!mySpeedPwm.setDuty(0.0F))
    {
        if (myPwmInitializedByDriver)
        {
            mySpeedPwm.deinit();
            myPwmInitializedByDriver = false;
        }

        return false;
    }

    myBrakePin.write(false);
    myIsInitialized = true;
    mySpeed = 0.0F;
    return setDirection(Direction::Forward);
}

bool A89301::deinit() noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    const bool stopped = stop(StopMode::Coast);
    bool pwmDeinitialized = true;

    if (myPwmInitializedByDriver)
    {
        pwmDeinitialized = mySpeedPwm.deinit();
        myPwmInitializedByDriver = false;
    }

    myIsInitialized = false;
    mySpeed = 0.0F;
    return stopped && pwmDeinitialized;
}

bool A89301::isInitialized() const noexcept
{
    return myIsInitialized;
}

bool A89301::setDirection(const Direction direction) noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    // DIR high orders the motor phases ABC, DIR low orders them ACB.
    const bool forward{Direction::Forward == direction};
    myDirectionPin.write(forward != myInvertDirection);
    myDirection = direction;
    return true;
}

bool A89301::setSpeed(const float speed, const StopMode mode) noexcept
{
    if (!myIsInitialized || !isSpeedValid(speed))
    {
        return false;
    }

    if (speed <= 0.0F)
    {
        return stop(mode);
    }

    // BRAKE overrides the speed input, so it must be released before driving.
    myBrakePin.write(false);

    if (!mySpeedPwm.setDuty(speed))
    {
        return false;
    }

    mySpeed = speed;
    return true;
}

bool A89301::stop(const StopMode mode) noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    const bool success = mySpeedPwm.setDuty(0.0F);
    myBrakePin.write(StopMode::Brake == mode);

    if (success)
    {
        mySpeed = 0.0F;
    }

    return success;
}

} // namespace driver::motor
