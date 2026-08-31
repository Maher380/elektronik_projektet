/**
 * @file l298n.cpp
 * @brief L298N motor driver implementation.
 */

#include "driver/motor/l298n.h"

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

/**
 * @brief Apply L298N input states for a direction.
 *
 * @param[in] input1 GPIO connected to IN1 or IN3.
 * @param[in] input2 GPIO connected to IN2 or IN4.
 * @param[in] direction Direction to apply.
 */
void writeDirection(driver::gpio::Interface& input1,
                    driver::gpio::Interface& input2,
                    const Direction direction) noexcept
{
    switch (direction)
    {
    case Direction::Forward:
        input1.write(true);
        input2.write(false);
        break;
    case Direction::Backward:
        input1.write(false);
        input2.write(true);
        break;
    }
}

/**
 * @brief Apply L298N input states for coasting.
 *
 * @param[in] input1 GPIO connected to IN1 or IN3.
 * @param[in] input2 GPIO connected to IN2 or IN4.
 */
void writeCoast(driver::gpio::Interface& input1, driver::gpio::Interface& input2) noexcept
{
    input1.write(false);
    input2.write(false);
}

/**
 * @brief Apply L298N input states for active braking.
 *
 * @param[in] input1 GPIO connected to IN1 or IN3.
 * @param[in] input2 GPIO connected to IN2 or IN4.
 */
void writeBrake(driver::gpio::Interface& input1, driver::gpio::Interface& input2) noexcept
{
    input1.write(true);
    input2.write(true);
}
} // namespace

L298n::L298n(driver::pwm::Interface& enablePwm,
             driver::gpio::Interface& input1,
             driver::gpio::Interface& input2) noexcept
    : myEnablePwm{enablePwm}
    , myInput1{input1}
    , myInput2{input2}
    , myIsInitialized{false}
    , myPwmInitializedByDriver{false}
    , myDirection{Direction::Forward}
    , mySpeed{0.0F}
{}

bool L298n::init() noexcept
{
    if (myIsInitialized || !myInput1.isInitialized() || !myInput2.isInitialized())
    {
        return false;
    }

    if (!myEnablePwm.isInitialized())
    {
        if (!myEnablePwm.init())
        {
            return false;
        }

        myPwmInitializedByDriver = true;
    }

    if (!myEnablePwm.setDuty(0.0F))
    {
        if (myPwmInitializedByDriver)
        {
            myEnablePwm.deinit();
            myPwmInitializedByDriver = false;
        }

        return false;
    }

    writeCoast(myInput1, myInput2);
    myDirection = Direction::Forward;
    mySpeed = 0.0F;
    myIsInitialized = true;
    return true;
}

bool L298n::deinit() noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    const bool stopped = stop(StopMode::Coast);
    bool pwmDeinitialized = true;

    if (myPwmInitializedByDriver)
    {
        pwmDeinitialized = myEnablePwm.deinit();
        myPwmInitializedByDriver = false;
    }

    myIsInitialized = false;
    mySpeed = 0.0F;
    return stopped && pwmDeinitialized;
}

bool L298n::isInitialized() const noexcept
{
    return myIsInitialized;
}

bool L298n::setDirection(const Direction direction) noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    const float previousSpeed = mySpeed;
    if ((previousSpeed > 0.0F) && !myEnablePwm.setDuty(0.0F))
    {
        return false;
    }

    writeDirection(myInput1, myInput2, direction);
    myDirection = direction;

    if ((previousSpeed > 0.0F) && !myEnablePwm.setDuty(previousSpeed))
    {
        mySpeed = 0.0F;
        return false;
    }

    return true;
}

bool L298n::setSpeed(const float speed, const StopMode mode) noexcept
{
    if (!myIsInitialized || !isSpeedValid(speed))
    {
        return false;
    }

    if (speed <= 0.0F)
    {
        return stop(mode);
    }

    if ((speed < mySpeed) && (mode == StopMode::Brake))
    {
        if (!stop(StopMode::Brake))
        {
            return false;
        }
    }

    writeDirection(myInput1, myInput2, myDirection);

    if (!myEnablePwm.setDuty(speed))
    {
        return false;
    }

    mySpeed = speed;
    return true;
}

bool L298n::stop(const StopMode mode) noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    bool success = false;
    switch (mode)
    {
    case StopMode::Coast:
        success = myEnablePwm.setDuty(0.0F);
        writeCoast(myInput1, myInput2);
        break;
    case StopMode::Brake:
        writeBrake(myInput1, myInput2);
        success = myEnablePwm.setDuty(1.0F);
        break;
    }

    if (success)
    {
        mySpeed = 0.0F;
    }

    return success;
}

} // namespace driver::motor
