/**
 * @file a3144.h
 * @brief Odometer driver using an A3144 Hall-effect sensor.
 */

#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"

#include "driver/gpio/interface.h"
#include "driver/odometer/interface.h"

namespace driver::odometer
{

/**
 * @brief Odometer implementation using an A3144 Hall-effect sensor.
 *
 * The A3144 has an open-collector, active-low output: it is pulled low while a
 * magnet (south pole) passes the sensor. Pulses are counted by a GPIO interrupt
 * on the falling edge, so no pulses are missed regardless of the main loop rate.
 * The GPIO should be configured as an input with pull-up (Direction::InputPullup),
 * then no external pull-up is required.
 *
 * The magnets do not have to be evenly spaced around the wheel. speed() times the
 * last whole revolution rather than the last gap between magnets, and one revolution
 * covers the full circumference wherever the magnets happen to sit, so uneven spacing
 * cancels exactly instead of appearing as speed that swings pulse to pulse. The window
 * slides on every pulse, so the reading still updates once per magnet; it lags a real
 * change in speed by up to half a revolution, which is the price of not having to know
 * where the magnets are.
 *
 * @attention The A3144 needs a 4.5-24 V supply. When supplied with 5 V the output
 *            is still safe for the 3.3 V ESP32-S3 input since it is open-collector
 *            and only pulled up to 3.3 V by the ESP32-S3.
 */
class A3144 final : public Interface
{
public:
    /**
     * @brief Constructor.
     *
     * @param[in] gpio Reference to the GPIO connected to the sensor output.
     * @param[in] config Odometer configuration.
     */
    A3144(gpio::Interface& gpio, const Config& config) noexcept;

    /**
     * @brief Destructor.
     */
    ~A3144() noexcept override;

    /**
     * @brief Initialize the odometer and start counting pulses.
     *
     * @return True if the odometer was initialized successfully, false otherwise.
     */
    bool init() noexcept override;

    /**
     * @brief Stop counting pulses.
     *
     * @return True if the odometer was deinitialized successfully, false otherwise.
     */
    bool deinit() noexcept override;

    /**
     * @brief Check if the odometer is initialized.
     *
     * @return True if initialized and counting, false otherwise.
     */
    bool isInitialized() const noexcept override;

    /**
     * @brief Get the number of pulses counted since init() or the last reset().
     *
     * @return Pulse count, 0 if not initialized.
     */
    std::uint32_t pulseCount() const noexcept override;

    /**
     * @brief Get the distance travelled since init() or the last reset().
     *
     * @return Distance in meters, 0 if not initialized.
     */
    float distance() const noexcept override;

    /**
     * @brief Get the current speed.
     * Calculated from the time the last whole wheel revolution took, so the result does
     * not depend on how the magnets are spaced. Until the first revolution after init()
     * or reset() is complete, it falls back to the time between the two latest pulses,
     * which is an estimate only: it assumes even spacing. Reads as 0 if no pulse has
     * been seen for a while.
     *
     * @return Speed in meters per second, 0 if standing still or not initialized.
     */
    float speed() const noexcept override;

    /**
     * @brief Reset the pulse count (and thereby the distance) to zero.
     */
    void reset() noexcept override;

    // Delete default constructor, copy/move constructors and assignment operators.
    A3144()                               = delete;
    A3144(const A3144&)            = delete;
    A3144(A3144&&)                 = delete;
    A3144& operator=(const A3144&) = delete;
    A3144& operator=(A3144&&)      = delete;

private:
    /**
     * @brief GPIO interrupt handler, called on every falling edge of the sensor output.
     *
     * @param[in] arg Pointer to the A3144 instance.
     */
    static void onPulse(void* arg) noexcept;

    /** GPIO connected to the sensor output. */
    gpio::Interface& myGpio;

    /** Distance travelled per pulse in meters, nominal: assumes evenly spaced magnets. */
    const float myDistancePerPulse;

    /** Wheel circumference in meters, the distance of one full revolution. */
    const float myDistancePerRevolution;

    /** Magnets on the wheel, at least 1 and at most MaxPulsesPerRevolution. */
    const std::uint8_t myPulsesPerRevolution;

    /** Guards the fields below, which are shared between the ISR and tasks. */
    mutable portMUX_TYPE myMux = portMUX_INITIALIZER_UNLOCKED;

    /** Number of pulses counted, the basis for distance; only reset() clears it. */
    std::uint32_t myPulseCount;

    /**
     * @brief Pulses seen since the speed window last started.
     *
     * Separate from myPulseCount because standing still restarts the window without
     * losing the distance travelled so far.
     */
    std::uint32_t myWindowPulses;

    /** Timestamp of the latest pulse in microseconds (esp_timer). */
    std::int64_t myLastPulseUs;

    /** Time between the two latest pulses in microseconds, 0 if fewer than two pulses. */
    std::int64_t myPulsePeriodUs;

    /**
     * @brief Timestamp of each of the last myPulsesPerRevolution pulses, in microseconds.
     *
     * Pulse n is written at index n % myPulsesPerRevolution, so the entry about to be
     * overwritten is the pulse exactly one revolution back.
     */
    std::int64_t myPulseTimesUs[MaxPulsesPerRevolution];

    /** Time the last whole revolution took in microseconds, 0 before the first one. */
    std::int64_t myRevolutionPeriodUs;

    /** True if init() has succeeded. */
    bool myInitialized;
};

} // namespace driver::odometer
