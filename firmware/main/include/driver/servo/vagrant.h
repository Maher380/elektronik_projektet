/**
 * @file vagrant.h
 * @brief Vagrant servo driver.
 */

#pragma once

#include "driver/servo/interface.h"

#include <cstdint>

namespace driver::pwm { class Interface; }

namespace driver::servo
{

/**
 * @brief Servo implementation controlled by PWM frequency.
 *
 * The Vagrant controller uses 215 Hz for maximum right, 300 Hz for forward,
 * and 500 Hz for maximum left.
 */
class Vagrant final : public Interface
{
public:
    explicit Vagrant(pwm::Interface& pwm) noexcept;
    ~Vagrant() noexcept override = default;

    bool init() noexcept override;
    bool deinit() noexcept override;
    bool isInitialized() const noexcept override;
    bool setDirection(float angleDegrees) noexcept override;
    float getDirection() const noexcept override;
    bool center() noexcept override;

    Vagrant(const Vagrant&) = delete;
    Vagrant& operator=(const Vagrant&) = delete;
    Vagrant(Vagrant&&) = delete;
    Vagrant& operator=(Vagrant&&) = delete;

private:
    // @Todo these angles will need to be modified after measurements of the actual servo.
    static constexpr float MinAngleDegrees{-90.0F};
    static constexpr float MaxAngleDegrees{90.0F};
    static constexpr std::uint32_t MinFrequencyHz{215U}; // probably 200 is the right value, but it begins to turn left below 215, likely due to mechanical error.
    static constexpr std::uint32_t CenterFrequencyHz{300U};
    static constexpr std::uint32_t MaxFrequencyHz{500U};

    pwm::Interface& myPwm;
    float myDirection{0.0F};
    bool myIsInitialized{false};
};

} // namespace driver::servo