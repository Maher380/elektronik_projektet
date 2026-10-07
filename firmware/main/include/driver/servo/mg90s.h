/**
 * @file mg90s.h
 * @brief MG90S servo driver.
 */

#pragma once

#include "driver/servo/interface.h"

#include <cstdint>

namespace driver::pwm { class Interface; }

namespace driver::servo
{

/**
 * @brief MG90S micro servo controlled by pulse width.
 *
 * The MG90S is a standard hobby servo: the pulse width sets the angle and the PWM
 * frequency only sets how often the pulse is sent. It expects 50 Hz; init() refuses a
 * PWM that runs much faster, since an analog servo overheats on too many pulses.
 * Angles outside -90 to 90 degrees are clamped to full lock.
 *
 * Supply 4.8-6 V. The servo draws about 1 A when it stalls, so the steering end
 * pulses must stay inside the mechanical end stops or the servo and its regulator
 * overheat.
 */
class Mg90s final : public Interface
{
public:
    explicit Mg90s(pwm::Interface& pwm) noexcept;
    ~Mg90s() noexcept override = default;

    bool init() noexcept override;
    bool deinit() noexcept override;
    bool isInitialized() const noexcept override;
    bool setDirection(float angleDegrees) noexcept override;
    float getDirection() const noexcept override;
    bool center() noexcept override;

    Mg90s(const Mg90s&) = delete;
    Mg90s& operator=(const Mg90s&) = delete;
    Mg90s(Mg90s&&) = delete;
    Mg90s& operator=(Mg90s&&) = delete;

private:
    static constexpr float MinAngleDegrees{-90.0F};
    static constexpr float MaxAngleDegrees{90.0F};

    // PWM frequencies init() accepts around the nominal 50 Hz.
    static constexpr std::uint32_t MinFrequencyHz{40U};
    static constexpr std::uint32_t MaxFrequencyHz{60U};

    // Full travel of the servo itself (about 180 degrees). Never send pulses outside this.
    static constexpr float MinPulseUs{500.0F};
    static constexpr float MaxPulseUs{2400.0F};

    // Measured on Ford with the `servo` serial command: the wheels point straight at about
    // 1931 us, and 2250 us is full right for now.
    // @Todo measure the left end stop. Until then left sits 300 us from center.
    static constexpr float LeftPulseUs{1631.0F};
    static constexpr float CenterPulseUs{1931.0F};
    static constexpr float RightPulseUs{2250.0F};

    static_assert(MinPulseUs <= LeftPulseUs && LeftPulseUs <= MaxPulseUs, "left pulse outside servo travel");
    static_assert(MinPulseUs <= RightPulseUs && RightPulseUs <= MaxPulseUs, "right pulse outside servo travel");
    static_assert(MinPulseUs <= CenterPulseUs && CenterPulseUs <= MaxPulseUs, "center pulse outside servo travel");

    pwm::Interface& myPwm;
    float myDirection{0.0F};
    bool myIsInitialized{false};
};

} // namespace driver::servo
