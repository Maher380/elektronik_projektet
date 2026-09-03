/**
 * @file esp32s3.h
 * @brief ESP32-S3 servo driver skeleton.
 */

#pragma once

#include "driver/servo/interface.h"

#include <cstdint>

namespace driver::pwm { class Interface; }

namespace driver::servo
{

/**
 * @brief Servo implementation backed by an ESP32-S3 PWM output.
 */
class Esp32s3 final : public Interface
{
public:
    explicit Esp32s3(pwm::Interface& pwm) noexcept;
    ~Esp32s3() noexcept override = default;

    bool init() noexcept override;
    bool deinit() noexcept override;
    bool isInitialized() const noexcept override;
    bool setDirection(float angleDegrees) noexcept override;
    float getDirection() const noexcept override;
    bool center() noexcept override;

    Esp32s3(const Esp32s3&) = delete;
    Esp32s3& operator=(const Esp32s3&) = delete;
    Esp32s3(Esp32s3&&) = delete;
    Esp32s3& operator=(Esp32s3&&) = delete;

private:
    static constexpr float MinAngleDegrees{-90.0F};
    static constexpr float MaxAngleDegrees{90.0F};
    static constexpr float MinDuty{0.025F};
    static constexpr float MaxDuty{0.125F};

    pwm::Interface& myPwm;
    float myDirection{0.0F};
    bool myIsInitialized{false};
};

} // namespace driver::servo
