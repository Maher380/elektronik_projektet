/**
 * @file stub.h
 * @brief Motor driver stub for host tests and simulation.
 */

#pragma once

#include "driver/motor/interface.h"

namespace driver::motor
{

/**
 * @brief Simulated motor driver implementation.
 */
class Stub final : public Interface
{
public:
    /**
     * @brief Constructor.
     */
    Stub() noexcept = default;

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Initialize the simulated motor driver.
     *
     * @return True if the stub was not already initialized, false otherwise.
     */
    bool init() noexcept override
    {
        if (myIsInitialized)
        {
            return false;
        }

        myIsInitialized = true;
        return true;
    }

    /**
     * @brief Deinitialize the simulated motor driver.
     *
     * @return True if the stub was initialized before the call, false otherwise.
     */
    bool deinit() noexcept override
    {
        if (!myIsInitialized)
        {
            return false;
        }

        myIsInitialized = false;
        mySpeed = 0.0F;
        return true;
    }

    /**
     * @brief Check if the simulated motor driver is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override
    {
        return myIsInitialized;
    }

    /**
     * @brief Store a simulated motor direction.
     *
     * @param[in] direction Direction to store.
     * @return True if initialized, false otherwise.
     */
    bool setDirection(Direction direction) noexcept override
    {
        if (!myIsInitialized)
        {
            return false;
        }

        myDirection = direction;
        return true;
    }

    /**
     * @brief Store a simulated normalized motor speed.
     *
     * @param[in] speed Speed in range 0.0f - 1.0f.
     * @param[in] mode Stop behavior to store if speed is zero.
     * @return True if initialized and speed is valid, false otherwise.
     */
    bool setSpeed(float speed, StopMode mode = StopMode::Coast) noexcept override
    {
        if (!myIsInitialized || !isSpeedValid(speed))
        {
            return false;
        }

        mySpeed = speed;
        if (speed <= 0.0F)
        {
            myStopMode = mode;
        }

        return true;
    }

    /**
     * @brief Store a simulated stop command.
     *
     * @param[in] mode Stop behavior to store.
     * @return True if initialized, false otherwise.
     */
    bool stop(StopMode mode = StopMode::Coast) noexcept override
    {
        if (!myIsInitialized)
        {
            return false;
        }

        mySpeed = 0.0F;
        myStopMode = mode;
        return true;
    }

    /**
     * @brief Read stored direction.
     *
     * @return Stored direction.
     */
    Direction direction() const noexcept
    {
        return myDirection;
    }

    /**
     * @brief Read stored speed.
     *
     * @return Stored speed in range 0.0f - 1.0f.
     */
    float speed() const noexcept
    {
        return mySpeed;
    }

    /**
     * @brief Read stored stop mode.
     *
     * @return Stored stop mode.
     */
    StopMode stopMode() const noexcept
    {
        return myStopMode;
    }

    Stub(const Stub&)            = delete;
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(Stub&&)      = delete;

private:
    /**
     * @brief Check if a speed value is inside the supported range.
     *
     * @param[in] speed Speed value to validate.
     * @return True if speed is between 0.0f and 1.0f.
     */
    static bool isSpeedValid(float speed) noexcept
    {
        return (speed >= 0.0F) && (speed <= 1.0F);
    }

    /** True if the simulated motor driver is initialized. */
    bool myIsInitialized{false};

    /** Simulated motor direction. */
    Direction myDirection{Direction::Forward};

    /** Simulated stop behavior. */
    StopMode myStopMode{StopMode::Coast};

    /** Simulated motor speed in range 0.0f - 1.0f. */
    float mySpeed{0.0F};
};

} // namespace driver::motor
