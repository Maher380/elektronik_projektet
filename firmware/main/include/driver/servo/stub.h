/**
 * @file stub.h
 * @brief Servo driver stub for host tests and simulation.
 */

#pragma once

#include "driver/servo/interface.h"

namespace driver::servo
{

/**
 * @brief Simulated servo driver implementation.
 */
class Stub final : public Interface
{
public:
    Stub() noexcept = default;
    ~Stub() noexcept override = default;

    bool init() noexcept override
    {
        if (myIsInitialized)
        {
            return false;
        }

        myIsInitialized = true;
        return true;
    }

    bool deinit() noexcept override
    {
        if (!myIsInitialized)
        {
            return false;
        }

        myIsInitialized = false;
        myDirection = 0.0F;
        return true;
    }

    bool isInitialized() const noexcept override
    {
        return myIsInitialized;
    }

    bool setDirection(float angleDegrees) noexcept override
    {
        if (!myIsInitialized || angleDegrees < MinAngleDegrees || angleDegrees > MaxAngleDegrees)
        {
            return false;
        }

        myDirection = angleDegrees;
        return true;
    }

    float getDirection() const noexcept override
    {
        return myDirection;
    }

    bool center() noexcept override
    {
        return setDirection(0.0F);
    }

    Stub(const Stub&) = delete;
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&) = delete;
    Stub& operator=(Stub&&) = delete;

private:
    static constexpr float MinAngleDegrees{-90.0F};
    static constexpr float MaxAngleDegrees{90.0F};

    bool myIsInitialized{false};
    float myDirection{0.0F};
};

} // namespace driver::servo
