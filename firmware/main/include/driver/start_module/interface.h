/**
 * @file interface.h
 * @brief Start module interface.
 */

#pragma once

#include <cstdint>

namespace driver::start_module
{
/**
 * @brief Start module interface.
 *
 * Tells whether the start signal has come from outside the car. Once started, the
 * module stays started until the car restarts: nothing the signal does later takes
 * the start back.
 */
class Interface
{
public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Sample the start signal. Call this regularly, for example once per logic tick.
     *
     * @param[in] nowMs Current time in milliseconds. It may wrap around.
     */
    virtual void update(std::uint32_t nowMs) noexcept = 0;

    /**
     * @brief Check whether the start signal has come.
     *
     * @return True once the car has been started, until the car restarts.
     */
    virtual bool isStarted() const noexcept = 0;

    /**
     * @brief Check if the start module is ready to use.
     *
     * @return True if ready, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;
};
} // namespace driver::start_module
