/**
 * @file l298n.h
 * @brief L298N motor driver interface implementation.
 */

#pragma once

#include "driver/gpio/interface.h"
#include "driver/motor/interface.h"
#include "driver/pwm/interface.h"

namespace driver::motor
{

/**
 * @brief Motor driver for one L298N H-bridge channel.
 *
 * The driver controls one motor using a referenced PWM output for ENA/ENB and
 * two referenced GPIO outputs for IN1/IN2. The PWM output controls speed, while
 * the GPIO outputs control direction and stop behavior.
 */
class L298n final : public Interface
{
public:
    /**
     * @brief Create an L298N motor driver.
     *
     * @param[in] enablePwm PWM driver connected to ENA or ENB.
     * @param[in] input1 GPIO output driver connected to IN1 or IN3.
     * @param[in] input2 GPIO output driver connected to IN2 or IN4.
     *
     * The referenced drivers must outlive this L298N driver instance.
     */
    L298n(driver::pwm::Interface& enablePwm,
          driver::gpio::Interface& input1,
          driver::gpio::Interface& input2) noexcept;

    /**
     * @brief Destructor.
     */
    ~L298n() noexcept override = default;

    /**
     * @brief Initialize the L298N driver and its referenced PWM/GPIO outputs.
     *
     * @return True if the driver was initialized successfully, false otherwise.
     */
    bool init() noexcept override;

    /**
     * @brief Deinitialize the L298N driver.
     *
     * @return True if the driver was deinitialized successfully, false otherwise.
     */
    bool deinit() noexcept override;

    /**
     * @brief Check if the L298N driver is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override;

    /**
     * @brief Set motor rotation direction.
     *
     * @param[in] direction Direction to drive the motor.
     * @return True if the direction was applied, false otherwise.
     */
    bool setDirection(Direction direction) noexcept override;

    /**
     * @brief Set normalized motor speed.
     *
     * @param[in] speed Speed in range 0.0f - 1.0f, where 1.0f is full output.
     * @param[in] mode Stop behavior to apply when reducing motor drive.
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
    L298n(const L298n&)            = delete;

    /** @brief Copy assignment is disabled because the driver binds to external output drivers. */
    L298n& operator=(const L298n&) = delete;

    /** @brief Move construction is disabled because output bindings are fixed. */
    L298n(L298n&&)                 = delete;

    /** @brief Move assignment is disabled because output bindings are fixed. */
    L298n& operator=(L298n&&)      = delete;

private:
    /** PWM driver connected to the L298N enable pin. */
    driver::pwm::Interface& myEnablePwm;

    /** GPIO output driver connected to the first L298N input pin. */
    driver::gpio::Interface& myInput1;

    /** GPIO output driver connected to the second L298N input pin. */
    driver::gpio::Interface& myInput2;

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
