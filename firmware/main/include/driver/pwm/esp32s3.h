/**
 * @file esp32s3.h
 * @brief ESP32-S3 MCPWM driver implementation.
 */

#pragma once

#include "driver/pwm/interface.h"

#include "driver/mcpwm_cmpr.h"
#include "driver/mcpwm_gen.h"
#include "driver/mcpwm_oper.h"
#include "driver/mcpwm_timer.h"

namespace driver::pwm
{

/**
 * @brief PWM driver for ESP32-S3 using the MCPWM peripheral.
 *
 * The driver owns one MCPWM timer, operator, comparator and generator. The
 * configured GPIO pin is reserved through the shared pin manager while the
 * driver is initialized.
 */
class Esp32s3 final : public Interface
{
public:
    /**
     * @brief Create a PWM driver with an explicit configuration.
     *
     * @param[in] config PWM output configuration.
     */
    explicit Esp32s3(Config config) noexcept;

    /**
     * @brief Create a PWM driver on a pin using default PWM settings.
     *
     * @param[in] pin GPIO pin used as PWM output.
     */
    explicit Esp32s3(std::uint8_t pin) noexcept;

    /**
     * @brief Destroy the PWM driver and release allocated MCPWM resources.
     */
    ~Esp32s3() noexcept override;

    /**
     * @brief Initialize MCPWM resources and start the PWM timer.
     *
     * @return True if initialization succeeded, false otherwise.
     */
    bool init() noexcept override;

    /**
     * @brief Stop PWM and release MCPWM resources.
     *
     * @return True if resources were released cleanly, false otherwise.
     */
    bool deinit() noexcept override;

    /**
     * @brief Check whether the PWM driver is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override;

    /**
     * @brief Set PWM duty cycle.
     *
     * @param[in] duty Duty cycle in range 0.0f - 1.0f.
     * @return True if the duty cycle was applied, false otherwise.
     */
    bool setDuty(float duty) noexcept override;

    /**
     * @brief Read the current duty cycle.
     *
     * @return Duty cycle in range 0.0f - 1.0f.
     */
    float duty() const noexcept override;

    /**
     * @brief Read the configured PWM frequency.
     *
     * @return Frequency in Hz.
     */
    std::uint32_t frequencyHz() const noexcept override;

    /** @brief Copy construction is disabled because the driver owns hardware handles. */
    Esp32s3(const Esp32s3&)            = delete;

    /** @brief Copy assignment is disabled because the driver owns hardware handles. */
    Esp32s3& operator=(const Esp32s3&) = delete;

    /** @brief Move construction is disabled because handle ownership is fixed. */
    Esp32s3(Esp32s3&&)                 = delete;

    /** @brief Move assignment is disabled because handle ownership is fixed. */
    Esp32s3& operator=(Esp32s3&&)      = delete;

private:
    /**
     * @brief Apply a duty cycle to the MCPWM comparator or forced output level.
     *
     * @param[in] duty Duty cycle in range 0.0f - 1.0f.
     * @return True if the duty cycle was applied, false otherwise.
     */
    bool applyDuty(float duty) noexcept;

    /**
     * @brief Release all allocated MCPWM resources and the reserved GPIO pin.
     *
     * @return True if every cleanup step succeeded, false otherwise.
     */
    bool releaseResources() noexcept;

    /** PWM output configuration. */
    Config myConfig;

    /** MCPWM timer period derived from resolution and frequency. */
    std::uint32_t myPeriodTicks;

    /** Current duty cycle in range 0.0f - 1.0f. */
    float myDuty;

    /** True after successful initialization. */
    bool myIsInitialized;

    /** True when the PWM GPIO pin is reserved in the pin manager. */
    bool myPinReserved;

    /** True after the MCPWM timer has been enabled. */
    bool myTimerEnabled;

    /** MCPWM timer handle. */
    mcpwm_timer_handle_t myTimer;

    /** MCPWM operator handle. */
    mcpwm_oper_handle_t myOperator;

    /** MCPWM comparator handle used for duty control. */
    mcpwm_cmpr_handle_t myComparator;

    /** MCPWM generator handle connected to the output GPIO. */
    mcpwm_gen_handle_t myGenerator;
};

} // namespace driver::pwm
