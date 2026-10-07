/**
 * @file gpio.h
 * @brief Start module read through one digital input.
 */

#pragma once

#include <cstdint>

#include "driver/gpio/interface.h"
#include "driver/start_module/interface.h"

namespace driver::start_module
{
/**
 * @brief Start module read through one digital input.
 *
 * The signal is low while the car waits, goes high to start it and low again to
 * stop it.
 *
 * - **Start:** counts only once the input has been read low and then read high on
 *   every update() for at least the hold time, which ignores short noise spikes. A
 *   signal that is already high at power-on does not count until it has gone low and
 *   high again, so a module that was not reset after the last run cannot start the car.
 * - **Stop:** counts on the first low read after the start, with no hold time: a
 *   false stop is safe, a missed one is not.
 *
 * Stopped is final until the car restarts.
 *
 * @note No pull-down is used: the start module must drive the line both low and
 *       high. A loose wire leaves the input floating and may give a false start.
 * @note The input must be 3.3 V. A 5 V module needs a divider, as on the SRF05 echo.
 *
 * Platform independent: works with any GPIO driver.
 */
class Gpio final : public Interface
{
public:
    /** Default time the signal must stay high before the start counts, in milliseconds. */
    static constexpr std::uint32_t DefaultHoldTimeMs{20U};

    /**
     * @brief Constructor.
     *
     * @param[in] input Reference to a GPIO input connected to the start module output.
     * @param[in] holdTimeMs Time the signal must stay high before the start counts,
     *                       in milliseconds. 0 counts the first high read after a low.
     */
    explicit Gpio(driver::gpio::Interface& input,
                  std::uint32_t holdTimeMs = DefaultHoldTimeMs) noexcept;

    /**
     * @brief Destructor.
     */
    ~Gpio() noexcept override = default;

    /**
     * @brief Sample the start signal. Does nothing if uninitialized or stopped.
     *
     * @param[in] nowMs Current time in milliseconds. It may wrap around.
     */
    void update(std::uint32_t nowMs) noexcept override;

    /**
     * @brief Get the state of the start module.
     *
     * @return Waiting until started, Started until stopped, then Stopped until the car restarts.
     */
    State state() const noexcept override;

    /**
     * @brief Check if the GPIO input is initialized.
     *
     * @return True if ready, false otherwise.
     */
    bool isInitialized() const noexcept override;

    Gpio()                       = delete; // No default constructor.
    Gpio(const Gpio&)            = delete; // No copy constructor.
    Gpio(Gpio&&)                 = delete; // No move constructor.
    Gpio& operator=(const Gpio&) = delete; // No copy assignment.
    Gpio& operator=(Gpio&&)      = delete; // No move assignment.

private:
    /** Sample the signal while waiting for the start. */
    void updateWaiting(bool high, std::uint32_t nowMs) noexcept;

    /** GPIO input connected to the start module output. */
    driver::gpio::Interface& myInput;
    /** Time the signal must stay high before the start counts, in milliseconds. */
    const std::uint32_t myHoldTimeMs;
    /** Time the signal was first read high after a low, in milliseconds. */
    std::uint32_t myHighSinceMs;
    /** True once the signal has been read low; a start needs a low first. */
    bool mySeenLow;
    /** True while the signal has been read high on every update since a low. */
    bool myHigh;
    /** Current state. It only moves forward; only a restart brings it back to Waiting. */
    State myState;
};
} // namespace driver::start_module
