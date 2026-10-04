/**
 * @file gaps.h
 * @brief Magnet gap fractions: one wheel's geometry, and the arithmetic over it.
 *
 * Everything here is pure: no hardware, no timers, no flash. That is deliberate, so
 * the parts that are easy to get wrong - deriving fractions, recovering the phase,
 * deciding whether a measurement is believable - can be tested on the host.
 */

#pragma once

#include <cstdint>

namespace driver::odometer
{

/**
 * @brief Largest magnet count a driver has to support.
 *
 * Drivers keep one pulse timestamp per magnet to measure a whole revolution, so the
 * count is bounded. Configurations above this are clamped.
 */
inline constexpr std::uint8_t MaxPulsesPerRevolution{16U};

/**
 * @brief How much of the circumference each gap between magnets spans.
 *
 * fraction[0] up to fraction[count - 1] are in the order the wheel passes them
 * going forward, and sum to 1. A count of 0 means "no table".
 *
 * Which physical magnet fraction[0] belongs to is not recorded and cannot be: one
 * sensor and identical magnets give no datum. Only the cyclic order is meaningful,
 * which is why the phase has to be recovered by matching, not by remembering.
 */
struct GapTable
{
    /** Magnets on the wheel; 0 means no table is held. */
    std::uint8_t count{0U};

    /** Fraction of a revolution for each gap, in forward order. */
    float fraction[MaxPulsesPerRevolution]{};
};

/** How far the fractions may stray from summing to 1 and still be believed. */
inline constexpr float GapSumTolerance{0.005F};

/** A gap smaller than this is taken as nonsense rather than a very close pair. */
inline constexpr float MinGapFraction{0.01F};

/**
 * @brief How much better the best rotation must fit than the runner-up.
 *
 * Ford's layout scores about 0.25 when correctly phased and 0 on a wheel with evenly
 * spaced magnets, which cannot be phased at all. Simulation over 20000 trials put no
 * wrong rotation above this threshold until the per-gap noise reached 0.04 of a
 * revolution, which is 14 degrees of error.
 */
inline constexpr float MinPhaseMargin{0.05F};

/**
 * @brief Revolutions that must agree on the same rotation before the phase is trusted.
 *
 * The margin alone leaks: at 0.04 noise about 0.9 % of locks were wrong. Because the
 * noise is independent from one revolution to the next, requiring three in a row took
 * that to zero in the same simulation.
 *
 * @attention "In a row" must mean windows a whole revolution apart. A driver's revolution
 *            window slides by one magnet per pulse, so consecutive windows share all but
 *            one of their gaps and would simply repeat each other's mistake rather than
 *            confirm it. A driver that checks every pulse gains nothing from this
 *            constant; it has to space its checks by the magnet count.
 */
inline constexpr std::uint8_t PhaseLockRevolutions{3U};

/**
 * @brief Build a table of equal gaps.
 *
 * @param[in] count Magnets on the wheel.
 * @return Table of count equal fractions, or an empty table if count is out of range.
 */
GapTable uniformTable(std::uint8_t count) noexcept;

/**
 * @brief Check a table could describe a real wheel.
 *
 * Rejects an empty or oversized count, a fraction that is not positive, and fractions
 * that do not sum to about 1. NaN fails every test it is given.
 *
 * @param[in] table Table to check.
 * @return True if the table is usable.
 */
bool isPlausible(const GapTable& table) noexcept;

/**
 * @brief Derive the gap fractions of the revolution that just ended.
 *
 * The caller holds the last count pulse timestamps, which bound count - 1 gaps. The
 * remaining gap is the one that ended at the oldest of them, and it is whatever the
 * revolution has left over - so all count gaps are recoverable, and they sum to 1 by
 * construction.
 *
 * Dividing by the revolution is what makes the result speed-independent: the same wheel
 * gives the same fractions at any steady speed.
 *
 * @param[in] orderedTimesUs The last count pulse timestamps, oldest first.
 * @param[in] count Magnets on the wheel.
 * @param[in] revolutionUs Duration of the revolution ending at the newest pulse.
 * @param[out] out count fractions, in the same order as the timestamps.
 * @return False if the inputs cannot describe a revolution.
 */
bool gapFractions(const std::int64_t* orderedTimesUs,
                  std::uint8_t count,
                  std::int64_t revolutionUs,
                  float* out) noexcept;

/**
 * @brief Find which rotation of a stored table the observed gaps match.
 *
 * @param[in] observed stored.count observed fractions.
 * @param[in] stored Table to match against.
 * @param[out] margin How much better the best rotation fit than the runner-up. The
 *                    caller compares this with MinPhaseMargin; a wheel whose gaps are
 *                    all equal scores 0 and must not be phased.
 * @return Rotation r such that observed[j] corresponds to stored.fraction[(j + r) %
 *         count], or -1 if the inputs are unusable.
 */
int bestRotation(const float* observed, const GapTable& stored, float& margin) noexcept;

/**
 * @brief Check several measurements of the same wheel agree.
 *
 * Used on tables measured at different wheel speeds. Real geometry does not change with
 * speed, so disagreement across speeds is the motor's roughness rather than the wheel's
 * shape, and a table that disagrees must not be stored.
 *
 * @param[in] tables Tables to compare.
 * @param[in] tableCount How many; at least 2.
 * @param[in] tolerance Largest spread in any one gap that still counts as agreement.
 * @param[out] worstGap Index of the gap that varied most.
 * @param[out] worstSpread How much it varied.
 * @return True if every gap's spread is within tolerance.
 */
bool tablesAgree(const GapTable* tables,
                 std::uint8_t tableCount,
                 float tolerance,
                 std::uint8_t& worstGap,
                 float& worstSpread) noexcept;

/**
 * @brief Average several tables of the same wheel, renormalised to sum to 1.
 *
 * @param[in] tables Tables to average.
 * @param[in] tableCount How many; at least 1.
 * @param[out] out The mean table.
 * @return False if the tables are unusable or disagree about the magnet count.
 */
bool meanTable(const GapTable* tables, std::uint8_t tableCount, GapTable& out) noexcept;

} // namespace driver::odometer
