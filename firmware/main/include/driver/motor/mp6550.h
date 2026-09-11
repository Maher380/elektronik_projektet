/**
 * @file mp6550.h
 * @brief motor driver for MP6550.
 */

#pragma once

#include "FreeRTOSConfig.h"
#include <cstdint>

#include "driver/motor/interface.h"
#include "driver/pwm/interface.h"

namespace driver::motor
{

/**
 * @brief Motor driver for MP6550.
 */
class MP6550: public Interface
{
public:
    /**
    * @brief Constructor.
    *
    * @param[in] forwardPin The pin to drive the motor forward.
    * @param[in] backwardPin The pin to drive the motor backward.
    */
    explicit MP6550(driver::pwm::Interface& forwardPwm , driver::pwm::Interface& backwardPwm) noexcept;


    /**
     * @brief Destructor.
     * Releases the pins reservered by the constructor
     */
    ~MP6550() noexcept override = default;

    /** @Todo behövs denna till något?
     * @brief Initialize the motor driver.
     *
     * @return True if the motor driver was initialized successfully, false otherwise.
     */
    bool init() noexcept override;

    /** @Todo behövs denna till något?
     * @brief Deinitialize the motor driver.
     *
     * @return True if the motor driver was deinitialized successfully, false otherwise.
     */
    bool deinit() noexcept override;

    /**
     * @brief Check if the motor driver is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override;

    /** @Todo behövs denna till något?
     * @brief Set motor rotation direction.
     *
     * @param[in] direction Direction to drive the motor.
     * @return True if the direction was accepted, false otherwise.
     */
    bool setDirection(Direction direction) noexcept override;

    /**
     * @brief Set normalized motor speed.
     *
     * @param[in] speed Speed in range 0.0f - 1.0f, where 1.0f is full output.
     * @param[in] mode Stop behavior to apply.
     * @return True if the speed was accepted, false otherwise.
     */
    bool setSpeed(float speed, StopMode mode = StopMode::Coast) noexcept override;

    /**
     * @brief Stop the motor.
     *
     * @param[in] mode Stop behavior to apply.
     * @return True if the stop command was accepted, false otherwise.
     */
    bool stop(StopMode mode = StopMode::Coast) noexcept override;

private:
    static constexpr float coast{0.0F};
    static constexpr float hardbreak{1.0F};

    driver::pwm::Interface& myForwardPwmDriver;
    driver::pwm::Interface& myBackwardPwmDriver;

    Direction myDirection;
    float mySpeed;

    bool myInitialized;

};

} // namespace driver::motor