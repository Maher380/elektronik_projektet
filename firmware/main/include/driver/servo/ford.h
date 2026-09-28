/**
 * @file ford.h
 * @brief Ford servo driver.
 */

#pragma once

#include "driver/servo/interface.h"

namespace driver::pwm { class Interface; }

namespace driver::servo
{

/**
 * @brief Servo implementation controlled by pulse width.
 *
 * The Ford servo is a standard hobby servo: the pulse width sets the angle and the
 * PWM frequency only sets how often the pulse is sent (50 Hz on Ford). Angles outside
 * -90 to 90 degrees are clamped to full lock.
 */
class Ford final : public Interface
{
public:
    explicit Ford(pwm::Interface& pwm) noexcept;
    ~Ford() noexcept override = default;

    bool init() noexcept override;
    bool deinit() noexcept override;
    bool isInitialized() const noexcept override;
    bool setDirection(float angleDegrees) noexcept override;
    float getDirection() const noexcept override;
    bool center() noexcept override;

    Ford(const Ford&) = delete;
    Ford& operator=(const Ford&) = delete;
    Ford(Ford&&) = delete;
    Ford& operator=(Ford&&) = delete;

private:
    static constexpr float MinAngleDegrees{-90.0F};
    static constexpr float MaxAngleDegrees{90.0F};
    // Measured on the bench with experiment/steering-test. The end stops are at about
    // 900-1000 us (left) and 2000-2100 us (right); these stay just inside them.
    static constexpr float LeftPulseUs{1000.0F};
    static constexpr float CenterPulseUs{1500.0F};
    static constexpr float RightPulseUs{2000.0F};

    pwm::Interface& myPwm;
    float myDirection{0.0F};
    bool myIsInitialized{false};
};

} // namespace driver::servo
