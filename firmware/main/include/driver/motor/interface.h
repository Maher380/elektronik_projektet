/**
 * @file interface.h
 * @brief Abstract motor driver interface.
 */

#pragma once

#include <cstdint>

namespace driver::motor
{

/**
 * @brief Motor rotation direction.
 */
enum class Direction : std::uint8_t
{
    Forward,  /**< Rotate the motor forward. */
    Backward, /**< Rotate the motor backward. */
};

/**
 * @brief Motor stop behavior.
 */
enum class StopMode : std::uint8_t
{
    Coast, /**< Disable motor drive and let the motor coast. */
    Brake, /**< Electrically brake the motor. */
};

/**
 * @brief Abstract interface for motor drivers.
 */
class Interface
{
public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Initialize the motor driver.
     *
     * @return True if the motor driver was initialized successfully, false otherwise.
     */
    virtual bool init() noexcept = 0;

    /**
     * @brief Deinitialize the motor driver.
     *
     * @return True if the motor driver was deinitialized successfully, false otherwise.
     */
    virtual bool deinit() noexcept = 0;

    /**
     * @brief Check if the motor driver is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;

    /**
     * @brief Set motor rotation direction.
     *
     * @param[in] direction Direction to drive the motor.
     * @return True if the direction was accepted, false otherwise.
     */
    virtual bool setDirection(Direction direction) noexcept = 0;

    /**
     * @brief Set normalized motor speed.
     *
     * @param[in] speed Speed in range 0.0f - 1.0f, where 1.0f is full output.
     * @param[in] mode Stop behavior to apply when reducing motor drive.
     * @return True if the speed was accepted, false otherwise.
     */
    virtual bool setSpeed(float speed, StopMode mode = StopMode::Coast) noexcept = 0;

    /**
     * @brief Stop the motor.
     *
     * @param[in] mode Stop behavior to apply.
     * @return True if the stop command was accepted, false otherwise.
     */
    virtual bool stop(StopMode mode = StopMode::Coast) noexcept = 0;
};

} // namespace driver::motor
