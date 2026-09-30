/**
 * @file a89301.h
 * @brief A89301 sensorless BLDC motor controller interface implementation.
 */

#pragma once

#include "driver/gpio/interface.h"
#include "driver/motor/interface.h"
#include "driver/pwm/interface.h"

namespace driver::motor
{

/**
 * @brief Motor driver for the Allegro A89301 sensorless BLDC controller (Pololu md47a board).
 *
 * The driver controls one brushless motor using a referenced PWM output for SPD
 * and two referenced GPIO outputs for DIR and BRAKE. The PWM duty cycle sets the
 * speed demand, DIR selects the phase order and BRAKE turns on all low-side
 * MOSFETs to electrically brake the motor.
 *
 * @attention The A89301 SPD input is configured for analog voltage by default.
 *            It must be set to PWM mode in EEPROM (e.g. with the Pololu A89301
 *            Configuration Utility) before this driver can control the speed.
 *
 * @note Duty cycles below the on/off threshold programmed in the A89301 EEPROM
 *       (6-20 %) stop the motor.
 */
class A89301 final : public Interface
{
public:
    /**
     * @brief Create an A89301 motor driver.
     *
     * @param[in] speedPwm PWM driver connected to SPD.
     * @param[in] direction GPIO output driver connected to DIR.
     * @param[in] brake GPIO output driver connected to BRAKE.
     * @param[in] invertDirection True to drive DIR low for Direction::Forward.
     *                            Which level is forward depends on the motor phase wiring.
     *
     * The referenced drivers must outlive this A89301 driver instance.
     */
    A89301(driver::pwm::Interface& speedPwm,
           driver::gpio::Interface& direction,
           driver::gpio::Interface& brake,
           bool invertDirection = false) noexcept;

    /**
     * @brief Destructor.
     */
    ~A89301() noexcept override = default;

    /**
     * @brief Initialize the A89301 driver and its referenced PWM output.
     *
     * The motor is left stopped in coast mode.
     *
     * @return True if the driver was initialized successfully, false otherwise.
     */
    bool init() noexcept override;

    /**
     * @brief Deinitialize the A89301 driver.
     *
     * @return True if the driver was deinitialized successfully, false otherwise.
     */
    bool deinit() noexcept override;

    /**
     * @brief Check if the A89301 driver is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override;

    /**
     * @brief Set motor rotation direction.
     *
     * The A89301 supports changing direction while the motor is running.
     *
     * @param[in] direction Direction to drive the motor.
     * @return True if the direction was applied, false otherwise.
     */
    bool setDirection(Direction direction) noexcept override;

    /**
     * @brief Set normalized motor speed.
     *
     * A speed above zero releases the brake. A speed of zero stops the motor.
     *
     * @param[in] speed Speed in range 0.0f - 1.0f, where 1.0f is the rated speed.
     * @param[in] mode Stop behavior to apply if speed is zero.
     * @return True if the speed was applied, false otherwise.
     */
    bool setSpeed(float speed, StopMode mode = StopMode::Coast) noexcept override;

    /**
     * @brief Stop the motor.
     *
     * @param[in] mode Stop behavior to apply.
     * @return True if the stop command was applied, false otherwise.
     */
    bool stop(StopMode mode = StopMode::Coast) noexcept override;

    /** @brief Copy construction is disabled because the driver binds to external output drivers. */
    A89301(const A89301&)            = delete;

    /** @brief Copy assignment is disabled because the driver binds to external output drivers. */
    A89301& operator=(const A89301&) = delete;

    /** @brief Move construction is disabled because output bindings are fixed. */
    A89301(A89301&&)                 = delete;

    /** @brief Move assignment is disabled because output bindings are fixed. */
    A89301& operator=(A89301&&)      = delete;

private:
    /** PWM driver connected to the A89301 SPD pin. */
    driver::pwm::Interface& mySpeedPwm;

    /** GPIO output driver connected to the A89301 DIR pin. */
    driver::gpio::Interface& myDirectionPin;

    /** GPIO output driver connected to the A89301 BRAKE pin. */
    driver::gpio::Interface& myBrakePin;

    /** True if DIR is driven low for Direction::Forward. */
    bool myInvertDirection;

    /** True after successful initialization. */
    bool myIsInitialized;

    /** True if this driver initialized the referenced PWM driver. */
    bool myPwmInitializedByDriver;

    /** Current motor direction. */
    Direction myDirection;

    /** Current speed in range 0.0f - 1.0f. */
    float mySpeed;
};

} // namespace driver::motor
