#include "driver/odometer/gaps.h"

#include <cstdint>

namespace driver::odometer
{
namespace
{
/** Absolute value, kept local so this file needs no <cmath>. */
constexpr float absOf(const float value) noexcept
{
    return (value < 0.0F) ? -value : value;
}

/** Larger than any possible fit error, which cannot exceed 2. */
constexpr float ErrorSentinel{1.0e9F};

} // namespace

// -----------------------------------------------------------------------------
GapTable uniformTable(const std::uint8_t count) noexcept
{
    GapTable table{};
    if ((count == 0U) || (count > MaxPulsesPerRevolution)) { return table; }

    table.count = count;
    const float share{1.0F / static_cast<float>(count)};
    for (std::uint8_t index{0U}; index < count; ++index) { table.fraction[index] = share; }

    return table;
}

// -----------------------------------------------------------------------------
bool isPlausible(const GapTable& table) noexcept
{
    if ((table.count == 0U) || (table.count > MaxPulsesPerRevolution)) { return false; }

    float sum{0.0F};
    for (std::uint8_t index{0U}; index < table.count; ++index)
    {
        const float fraction{table.fraction[index]};

        // Written as negated comparisons so that a NaN fails rather than passes.
        if (!(fraction >= MinGapFraction) || !(fraction <= 1.0F)) { return false; }

        sum += fraction;
    }

    const float error{sum - 1.0F};
    return (error <= GapSumTolerance) && (error >= -GapSumTolerance);
}

// -----------------------------------------------------------------------------
bool gapFractions(const std::int64_t* orderedTimesUs,
                  const std::uint8_t count,
                  const std::int64_t revolutionUs,
                  float* out) noexcept
{
    if ((orderedTimesUs == nullptr) || (out == nullptr)) { return false; }
    if ((count == 0U) || (count > MaxPulsesPerRevolution)) { return false; }
    if (revolutionUs <= 0) { return false; }

    // The timestamps span count - 1 gaps. The gap that ended at the oldest of them
    // started at a pulse that has since been overwritten, so it is the remainder.
    const std::int64_t spanUs{orderedTimesUs[count - 1U] - orderedTimesUs[0]};
    if ((spanUs < 0) || (spanUs >= revolutionUs)) { return false; }

    const float revolution{static_cast<float>(revolutionUs)};
    out[0] = static_cast<float>(revolutionUs - spanUs) / revolution;

    for (std::uint8_t index{1U}; index < count; ++index)
    {
        const std::int64_t gapUs{orderedTimesUs[index] - orderedTimesUs[index - 1U]};
        if (gapUs <= 0) { return false; }

        out[index] = static_cast<float>(gapUs) / revolution;
    }

    return true;
}

// -----------------------------------------------------------------------------
int bestRotation(const float* observed, const GapTable& stored, float& margin) noexcept
{
    margin = 0.0F;
    if ((observed == nullptr) || !isPlausible(stored)) { return -1; }

    const std::uint8_t count{stored.count};

    // One magnet has one gap, which is the whole revolution: the phase is trivial and
    // there is no runner-up to compare with.
    if (count == 1U)
    {
        margin = 1.0F;
        return 0;
    }

    float bestError{ErrorSentinel};
    float secondError{ErrorSentinel};
    int best{-1};

    for (std::uint8_t rotation{0U}; rotation < count; ++rotation)
    {
        float error{0.0F};
        for (std::uint8_t index{0U}; index < count; ++index)
        {
            const std::uint8_t shifted{static_cast<std::uint8_t>((index + rotation) % count)};
            error += absOf(observed[index] - stored.fraction[shifted]);
        }

        if (error < bestError)
        {
            secondError = bestError;
            bestError = error;
            best = static_cast<int>(rotation);
        }
        else if (error < secondError)
        {
            secondError = error;
        }
    }

    if (best < 0) { return -1; } // Only reachable if every error was NaN.

    margin = secondError - bestError;
    return best;
}

// -----------------------------------------------------------------------------
bool tablesAgree(const GapTable* tables,
                 const std::uint8_t tableCount,
                 const float tolerance,
                 std::uint8_t& worstGap,
                 float& worstSpread) noexcept
{
    worstGap = 0U;
    worstSpread = 0.0F;

    if ((tables == nullptr) || (tableCount < 2U)) { return false; }
    if (!isPlausible(tables[0])) { return false; }

    const std::uint8_t count{tables[0].count};
    for (std::uint8_t table{1U}; table < tableCount; ++table)
    {
        if (!isPlausible(tables[table]) || (tables[table].count != count)) { return false; }
    }

    for (std::uint8_t index{0U}; index < count; ++index)
    {
        float lowest{tables[0].fraction[index]};
        float highest{lowest};

        for (std::uint8_t table{1U}; table < tableCount; ++table)
        {
            const float fraction{tables[table].fraction[index]};
            if (fraction < lowest) { lowest = fraction; }
            if (fraction > highest) { highest = fraction; }
        }

        const float spread{highest - lowest};
        if (spread > worstSpread)
        {
            worstSpread = spread;
            worstGap = index;
        }
    }

    return worstSpread <= tolerance;
}

// -----------------------------------------------------------------------------
bool meanTable(const GapTable* tables, const std::uint8_t tableCount, GapTable& out) noexcept
{
    out = GapTable{};

    if ((tables == nullptr) || (tableCount == 0U)) { return false; }
    if (!isPlausible(tables[0])) { return false; }

    const std::uint8_t count{tables[0].count};
    for (std::uint8_t table{1U}; table < tableCount; ++table)
    {
        if (!isPlausible(tables[table]) || (tables[table].count != count)) { return false; }
    }

    float total{0.0F};
    for (std::uint8_t index{0U}; index < count; ++index)
    {
        float sum{0.0F};
        for (std::uint8_t table{0U}; table < tableCount; ++table)
        {
            sum += tables[table].fraction[index];
        }

        const float mean{sum / static_cast<float>(tableCount)};
        out.fraction[index] = mean;
        total += mean;
    }

    if (!(total > 0.0F)) { return false; }

    // Averaging leaves the sum a hair off 1; renormalise so the table stays plausible.
    for (std::uint8_t index{0U}; index < count; ++index) { out.fraction[index] /= total; }

    out.count = count;
    return isPlausible(out);
}

} // namespace driver::odometer
