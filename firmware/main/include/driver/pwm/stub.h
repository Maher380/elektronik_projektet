/**
 * @file stub.h
 * @brief PWM driver stub for host tests and simulation.
 */

#pragma once

#include "driver/pwm/interface.h"

namespace driver::pwm
{

/**
 * @brief Simulated PWM driver implementation.
 *
 * Stores initialization state and duty cycle in memory without touching
 * hardware.
 */
class Stub final : public Interface
{
public:
    /**
     * @brief Create a PWM stub with the provided configuration.
     *
     * @param[in] config Simulated PWM configuration.
     */
    explicit Stub(Config config = {}) noexcept
        : myConfig{config}
    {}

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Initialize the simulated PWM instance.
     *
     * @return True if the configuration is valid and the stub was not already initialized.
     */
    bool init() noexcept override
    {
        if (myIsInitialized || !isConfigValid())
        {
            return false;
        }

        myIsInitialized = true;
        return true;
    }

    /**
     * @brief Deinitialize the simulated PWM instance.
     *
     * @return True if the stub was initialized before the call.
     */
    bool deinit() noexcept override
    {
        if (!myIsInitialized)
        {
            return false;
        }

        myIsInitialized = false;
        myDuty = 0.0F;
        return true;
    }

    /**
     * @brief Check if the simulated PWM instance is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override
    {
        return myIsInitialized;
    }

    /**
     * @brief Store a simulated duty cycle.
     *
     * @param[in] duty Duty cycle in range 0.0f - 1.0f.
     * @return True if initialized and the duty cycle is valid.
     */
    bool setDuty(float duty) noexcept override
    {
        if (!myIsInitialized || !isDutyValid(duty))
        {
            return false;
        }

        myDuty = duty;
        return true;
    }

    /**
     * @brief Read the simulated duty cycle.
     *
     * @return Stored duty cycle.
     */
    float duty() const noexcept override
    {
        return myDuty;
    }

    /**
     * @brief Read the configured simulated PWM frequency.
     *
     * @return Frequency in Hz.
     */
    std::uint32_t frequencyHz() const noexcept override
    {
        return myConfig.frequencyHz;
    }

    bool setFrequencyHz(const std::uint32_t frequencyHz) noexcept override
    {
        if (frequencyHz == 0U || frequencyHz > myConfig.resolutionHz)
        {
            return false;
        }

        myConfig.frequencyHz = frequencyHz;
        return true;
    }

    Stub(const Stub&)            = delete;
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(Stub&&)      = delete;

private:
    /**
     * @brief Check whether the stored PWM configuration is usable.
     *
     * @return True if frequency and resolution can produce a PWM period.
     */
    bool isConfigValid() const noexcept
    {
        return (myConfig.frequencyHz > 0U) && (myConfig.resolutionHz >= myConfig.frequencyHz);
    }

    /**
     * @brief Check if a duty cycle is inside the supported range.
     *
     * @param[in] duty Duty cycle to validate.
     * @return True if duty is between 0.0f and 1.0f.
     */
    static bool isDutyValid(float duty) noexcept
    {
        return (duty >= 0.0F) && (duty <= 1.0F);
    }

    /** Simulated PWM configuration. */
    Config myConfig;

    /** True if the simulated PWM instance has been initialized. */
    bool myIsInitialized{false};

    /** Simulated duty cycle in range 0.0f - 1.0f. */
    float myDuty{0.0F};
};
} // namespace driver::pwm
