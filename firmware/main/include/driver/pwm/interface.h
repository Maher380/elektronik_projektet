/**
 * @file interface.h
 * @brief Abstract PWM driver interface.
 */

#pragma once

#include <cstdint>

namespace driver::pwm
{

/**
 * @brief Default PWM frequency used for motor control.
 */
inline constexpr std::uint32_t DefaultFrequencyHz{20'000U};

/**
 * @brief Default MCPWM timer resolution in ticks per second.
 */
inline constexpr std::uint32_t DefaultResolutionHz{1'000'000U};

/**
 * @brief Configuration for a PWM output.
 */
struct Config
{
    /** GPIO pin used as PWM output. */
    std::uint8_t pin{0U};

    /** PWM frequency in Hz. */
    std::uint32_t frequencyHz{DefaultFrequencyHz};

    /** MCPWM timer resolution in ticks per second. */
    std::uint32_t resolutionHz{DefaultResolutionHz};

    /** MCPWM hardware group ID. */
    std::uint32_t groupId{0U};

    /** True to invert the generated PWM output signal. */
    bool invertOutput{false};
};

/**
 * @brief Abstract interface for PWM drivers.
 */
class Interface
{

public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Initiate PWM.
     *
     * @return True if the PWM instance was initialized successfully, false otherwise.
     */
    virtual bool init() noexcept = 0;

    /**
     * @brief Deinitiate PWM instance.
     *
     * @return True if the PWM instance was deinitialized successfully, false otherwise.
     */
    virtual bool deinit() noexcept = 0;

    /**
     * @brief Check if PWM is enabled.
     *
     * @return True if enabled, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;

    /**
     * @brief Set duty cycle.
     *
     * @param[in] duty The duty cycle to set in range (0.0f - 1.0f)
     *                 0.5f = 50% duty cycle.
     * @return True if duty was accepted, false otherwise.
     */
    virtual bool setDuty(float duty) noexcept = 0;

    /**
     * @brief Read the currently configured duty cycle.
     *
     * @return Duty cycle in range 0.0f - 1.0f.
     */
    virtual float duty() const noexcept = 0;

    /**
     * @brief Read configured PWM frequency.
     *
     * @return Frequency in Hz.
     */
    virtual std::uint32_t frequencyHz() const noexcept = 0;

    /**
     * @brief Set the PWM frequency.
     *
     * @param[in] frequencyHz Frequency in Hz.
     * @return True if the frequency was accepted.
     */
    virtual bool setFrequencyHz(std::uint32_t frequencyHz) noexcept = 0;

};
} // namespace driver::pwm
