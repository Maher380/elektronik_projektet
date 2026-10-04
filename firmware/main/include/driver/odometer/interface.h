/**
 * @file interface.h
 * @brief Abstract odometer driver interface.
 */

#pragma once

#include <cstdint>

#include "driver/odometer/gaps.h"

namespace driver::odometer
{

/**
 * @brief Configuration for an odometer.
 */
struct Config
{
    /** Number of pulses the sensor produces per wheel revolution (i.e. number of magnets). */
    std::uint8_t pulsesPerRevolution{1U};

    /** Wheel diameter in meters. */
    float wheelDiameterM{0.0F};
};

/**
 * @brief Ratio of a circle's circumference to its diameter.
 */
inline constexpr float Pi{3.14159265358979323846F};

/**
 * @brief Compute the distance the wheel rolls in one full revolution from a Config.
 *
 * @param[in] config Odometer configuration.
 * @return Wheel circumference in meters.
 */
inline constexpr float distancePerRevolution(const Config& config) noexcept
{
    return Pi * config.wheelDiameterM;
}

/**
 * @brief Compute the nominal distance travelled per sensor pulse from a Config.
 *
 * @attention This is the circumference shared equally between the magnets. It is the
 *            true distance for a given pulse only when the magnets are evenly spaced.
 *            Unevenly spaced magnets make it an average: a count of whole revolutions
 *            is still exact, and a part-revolution count is off by at most the worst
 *            spacing error, which does not accumulate. Prefer
 *            distancePerRevolution() for anything timed, such as speed.
 *
 * @param[in] config Odometer configuration.
 * @return Distance in meters per pulse, 0 if pulsesPerRevolution is 0.
 */
inline constexpr float distancePerPulse(const Config& config) noexcept
{
    if (config.pulsesPerRevolution == 0U) { return 0.0F; }

    return distancePerRevolution(config) / static_cast<float>(config.pulsesPerRevolution);
}

/**
 * @brief What a reported speed was worked out from.
 *
 * Reported alongside the speed so an operator can see which one they are looking at:
 * the two differ in how much they lag, not in whether they are correct.
 */
enum class SpeedSource : std::uint8_t
{
    /** Standing still, or too few pulses to know a speed yet. */
    None,

    /** Timed over a whole revolution. Always correct, lags half a revolution. */
    Revolution,

    /** Timed over one gap and corrected by its stored fraction. Lags half a gap. */
    PerGap,
};

/**
 * @brief Abstract interface for odometer drivers.
 */
class Interface
{

public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Initialize the odometer and start counting pulses.
     *
     * @return True if the odometer was initialized successfully, false otherwise.
     */
    virtual bool init() noexcept = 0;

    /**
     * @brief Stop counting pulses and release the hardware.
     *
     * @return True if the odometer was deinitialized successfully, false otherwise.
     */
    virtual bool deinit() noexcept = 0;

    /**
     * @brief Check if the odometer is initialized.
     *
     * @return True if initialized and counting, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;

    /**
     * @brief Get the number of pulses counted since init() or the last reset().
     *
     * @return Pulse count, 0 if not initialized.
     */
    virtual std::uint32_t pulseCount() const noexcept = 0;

    /**
     * @brief Get the distance travelled since init() or the last reset().
     *
     * @return Distance in meters, 0 if not initialized.
     */
    virtual float distance() const noexcept = 0;

    /**
     * @brief Get the current speed.
     *
     * Implementations measure over a whole number of wheel revolutions where they can,
     * so that uneven magnet spacing does not show up as speed that varies pulse to
     * pulse while the wheel turns at a constant rate.
     *
     * @return Speed in meters per second, 0 if standing still or not initialized.
     */
    virtual float speed() const noexcept = 0;

    /**
     * @brief Reset the pulse count (and thereby the distance) to zero.
     */
    virtual void reset() noexcept = 0;

    /**
     * @brief Do the per-revolution bookkeeping that cannot run in an interrupt.
     *
     * Recovering which gap the wheel is in means deriving fractions and matching them
     * against the stored table, which is floating-point work and belongs in a task. Call
     * it regularly from the driving loop; it returns at once when no new revolution has
     * completed.
     *
     * @note Never calling it is safe. Without it the phase is never established, so
     *       speed() stays on the revolution window - late, but never wrong.
     */
    virtual void update() noexcept = 0;

    /**
     * @brief Give the driver the measured gap fractions for its wheel.
     *
     * @param[in] table Gap table; an implausible one is refused and the previous kept.
     * @return True if the table was accepted.
     */
    virtual bool setGapTable(const GapTable& table) noexcept = 0;

    /**
     * @brief Tell the driver which way the wheel is being driven.
     *
     * An Odometer cannot see direction, so it has to be told. A change discards the
     * phase, because the gaps are then traversed in the opposite order.
     *
     * @param[in] forward True if the car is being driven forwards.
     */
    virtual void setForward(bool forward) noexcept = 0;

    /**
     * @brief What the latest speed() was worked out from.
     *
     * @return The source, so telemetry can report which reading the operator is seeing.
     */
    virtual SpeedSource speedSource() const noexcept = 0;

    /**
     * @brief How many times the driver has given up a phase it had established.
     *
     * Each one is a revolution spent back on the slower reading. The count is the best
     * available measure of how well the magnets are mounted, since a missed pulse is the
     * usual cause.
     *
     * @return Number of times the phase has been lost since init() or reset().
     */
    virtual std::uint32_t phaseLossCount() const noexcept = 0;

    /**
     * @brief Read the gap fractions of the revolution that just completed.
     *
     * The counterpart of setGapTable(). An Odometer that can be handed a calibration must
     * also be able to produce one, or the only code able to measure its own wheel is code
     * that knows which concrete driver it is holding - which is how the magnet gap
     * calibration came to live outside the logic layer. See ADR 0009.
     *
     * Indexed so that out[i] is always the same physical gap for as long as no pulse is
     * missed, which is what lets a calibration session average over many revolutions. The
     * indices are arbitrary - nothing on the car can say which magnet is which - but they
     * are consistent, and a consistent cyclic order is all a gap table needs.
     *
     * @param[out] out Buffer for count fractions.
     * @param[in] count Must equal the configured magnet count.
     * @return False if no whole revolution has been measured yet, or on a bad size.
     */
    virtual bool observedGaps(float* out, std::uint8_t count) const noexcept = 0;
};

} // namespace driver::odometer
