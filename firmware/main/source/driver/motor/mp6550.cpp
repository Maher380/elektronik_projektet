/**
 * @file mp6550.cpp
 * @brief imnplementation of motor driver for MP6550.
 */

 #include <cstdint>
 #include <cmath>

 #include "driver/motor/interface.h"
 #include "driver/motor/mp6550.h"
 #include "driver/pwm/interface.h"

namespace driver::motor
{

    /**
    * @brief Constructor.
    *
    * @param[in] forwardPin The pin to drive the motor forward.
    * @param[in] backwardPin The pin to drive the motor backward.
    */
    MP6550::MP6550(driver::pwm::Interface& forwardPwm, driver::pwm::Interface& backwardPwm) noexcept
    : myForwardPwmDriver{forwardPwm}
    , myBackwardPwmDriver{backwardPwm}
    , myDirection{Direction::Forward}
    , mySpeed{0.0F}
    , myInitialized{false}
{}

    /** @Todo behövs denna till något?
     * @brief Initialize the motor driver.
     *
     * @return True if the motor driver was initialized successfully, false otherwise.
     */
    bool MP6550::init() noexcept 
    {
        if (myInitialized)
            return true;

        if (!myForwardPwmDriver.isInitialized())
            if (!myForwardPwmDriver.init())
                return false;

        if (!myBackwardPwmDriver.isInitialized())
            if (!myBackwardPwmDriver.init())
            {
                myForwardPwmDriver.deinit();
                return false;
            }
        
        myInitialized = true;
        
        return myInitialized;
    }

    /** @Todo behövs denna till något?
     * @brief Deinitialize the motor driver.
     *
     * @return True if the motor driver was deinitialized successfully, false otherwise.
     */
    bool MP6550::deinit() noexcept 
    {
        myForwardPwmDriver.deinit();
        myBackwardPwmDriver.deinit();
        myInitialized = false;
        return true;

    }

    /**
     * @brief Check if the motor driver is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool MP6550::isInitialized() const noexcept
    {
        return myInitialized;
    }

    /** @Todo behövs denna till något?
     * @brief Set motor rotation direction.
     *
     * @param[in] direction Direction to drive the motor.
     * @return True if the direction was accepted, false otherwise.
     */
    bool MP6550::setDirection(Direction direction) noexcept
    {
        myDirection = direction;
        return true;
    }

    /**
     * @brief Set normalized motor speed.
     *
     * @param[in] speed Speed in range 0.0f - 1.0f, where 1.0f is full output.
     * @param[in] mode Stop behavior to apply.
     * @return True if the speed was accepted, false otherwise.
     */
    bool MP6550::setSpeed(float speed, StopMode mode ) noexcept
    {
        if (!myInitialized || !std::isfinite(speed) || speed < 0.0F || speed > 1.0F)
        {
            return false;
        }
        const float idleDuty = mode == StopMode::Brake ? hardbreak : coast;
        const float forward = myDirection == Direction::Forward ? speed : idleDuty;
        const float backward = myDirection == Direction::Forward ? idleDuty : speed;

        // Attempt both outputs, report either failure, and avoid redundant writes.
        const bool forwardOk = myForwardPwmDriver.isInitialized()
            && (myForwardPwmDriver.duty() == forward || myForwardPwmDriver.setDuty(forward));
        const bool backwardOk = myBackwardPwmDriver.isInitialized()
            && (myBackwardPwmDriver.duty() == backward || myBackwardPwmDriver.setDuty(backward));
        if (forwardOk && backwardOk) { mySpeed = speed; }
        return forwardOk && backwardOk;
    }

    /**
     * @brief Stop the motor.
     *
     * @param[in] mode Stop behavior to apply.
     * @return True if the stop command was accepted, false otherwise.
     */
    bool MP6550::stop(StopMode mode) noexcept
    {
        if (!myInitialized) { return false; }
        const float duty = mode == StopMode::Brake ? hardbreak : coast;
        const bool forwardOk = myForwardPwmDriver.isInitialized()
            && (myForwardPwmDriver.duty() == duty || myForwardPwmDriver.setDuty(duty));
        const bool backwardOk = myBackwardPwmDriver.isInitialized()
            && (myBackwardPwmDriver.duty() == duty || myBackwardPwmDriver.setDuty(duty));
        if (forwardOk && backwardOk) { mySpeed = 0.0F; }
        return forwardOk && backwardOk;
    }

} // namespace driver::motor
