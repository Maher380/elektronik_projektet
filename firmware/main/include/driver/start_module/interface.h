/**
 * @file interface.h
 * @brief Start module interface.
 */

#pragma once

#include <cstdint>

namespace driver::start_module
{
/**
 * @brief State of the start module.
 *
 * The state only moves forward, Waiting → Started → Stopped. Only a restart of
 * the car brings it back to Waiting.
 */
enum class State : std::uint8_t
{
    /** No start signal yet: the car must not drive. */
    Waiting,

    /** The start signal has come: the car may drive. */
    Started,

    /** The stop signal has come after a start: the car must stop until it restarts. */
    Stopped,
};

/**
 * @brief Start module interface.
 *
 * Tells whether the start and stop signals have come from outside the car.
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
     * @brief Get the state of the start module.
     *
     * @return Waiting until started, Started until stopped, then Stopped until the car restarts.
     */
    virtual State state() const noexcept = 0;

    /**
     * @brief Check if the start module is ready to use.
     *
     * @return True if ready, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;
};
} // namespace driver::start_module
