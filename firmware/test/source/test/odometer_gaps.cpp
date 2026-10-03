#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "driver/odometer/gaps.h"
#include "test/odometer_gaps.h"

namespace
{
using driver::odometer::GapTable;

bool expect(const bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("Odometer gaps test failed: %s\n", message);
        return false;
    }

    return true;
}

bool isNear(const float value, const float expected, const float tolerance = 0.0005F) noexcept
{
    return std::fabs(value - expected) < tolerance;
}

/** Ford's layout: four narrow gaps of an eighth and two wide ones of a quarter. */
GapTable fordTable() noexcept
{
    GapTable table{};
    table.count = 6U;
    const float ford[6]{0.125F, 0.125F, 0.25F, 0.25F, 0.125F, 0.125F};
    for (std::uint8_t index{0U}; index < 6U; ++index) { table.fraction[index] = ford[index]; }

    return table;
}

/**
 * @brief Pulse timestamps for one revolution of a wheel, at a chosen speed.
 *
 * The caller gets the last `count` timestamps, oldest first, as the odometer's ring
 * holds them, plus the revolution duration. Which gap comes first is set by `rotation`,
 * so a test can start the wheel anywhere.
 */
void simulateRevolution(const GapTable& table,
                        const std::int64_t revolutionUs,
                        const std::uint8_t rotation,
                        const std::int64_t startUs,
                        std::int64_t* timesOut) noexcept
{
    const std::uint8_t count{table.count};

    // The oldest stored timestamp is one gap into the revolution, because the pulse
    // that began the revolution has already been overwritten in the ring.
    std::int64_t now{startUs};
    for (std::uint8_t index{0U}; index < count; ++index)
    {
        const std::uint8_t gap{static_cast<std::uint8_t>((index + rotation) % count)};
        now += static_cast<std::int64_t>(
            static_cast<double>(table.fraction[gap]) * static_cast<double>(revolutionUs));
        timesOut[index] = now;
    }
}

bool testUniformAndPlausibility() noexcept
{
    bool passed{true};

    const GapTable uniform{driver::odometer::uniformTable(6U)};
    passed = passed && expect(uniform.count == 6U, "uniformTable should keep the count");
    passed = passed && expect(isNear(uniform.fraction[0], 1.0F / 6.0F), "uniform gap should be a sixth");
    passed = passed && expect(driver::odometer::isPlausible(uniform), "a uniform table should be plausible");

    passed = passed && expect(driver::odometer::uniformTable(0U).count == 0U, "0 magnets should give no table");
    passed = passed
        && expect(driver::odometer::uniformTable(driver::odometer::MaxPulsesPerRevolution + 1U).count == 0U,
                  "too many magnets should give no table");

    const GapTable single{driver::odometer::uniformTable(1U)};
    passed = passed && expect(driver::odometer::isPlausible(single), "one magnet, one whole-revolution gap");

    passed = passed && expect(driver::odometer::isPlausible(fordTable()), "Ford's table should be plausible");

    // A table that does not sum to 1 describes no wheel.
    GapTable short_{fordTable()};
    short_.fraction[0] = 0.05F;
    passed = passed && expect(!driver::odometer::isPlausible(short_), "fractions summing below 1 should be rejected");

    GapTable negative{fordTable()};
    negative.fraction[2] = -0.25F;
    passed = passed && expect(!driver::odometer::isPlausible(negative), "a negative gap should be rejected");

    GapTable nan{fordTable()};
    nan.fraction[3] = std::numeric_limits<float>::quiet_NaN();
    passed = passed && expect(!driver::odometer::isPlausible(nan), "a NaN gap should be rejected");

    GapTable empty{};
    passed = passed && expect(!driver::odometer::isPlausible(empty), "an empty table should be rejected");

    return passed;
}

bool testFractionsFromTimestamps() noexcept
{
    bool passed{true};
    const GapTable ford{fordTable()};

    // The same wheel at two very different speeds must give the same fractions: that
    // speed-independence is the whole reason for dividing by the revolution.
    const std::int64_t slowUs{640'000};
    const std::int64_t fastUs{36'000};

    for (std::uint8_t rotation{0U}; rotation < 6U; ++rotation)
    {
        std::int64_t slow[6]{};
        std::int64_t fast[6]{};
        simulateRevolution(ford, slowUs, rotation, 5'000'000, slow);
        simulateRevolution(ford, fastUs, rotation, 91'000'000, fast);

        float slowOut[6]{};
        float fastOut[6]{};
        passed = passed
            && expect(driver::odometer::gapFractions(slow, 6U, slowUs, slowOut), "slow fractions should derive")
            && expect(driver::odometer::gapFractions(fast, 6U, fastUs, fastOut), "fast fractions should derive");

        for (std::uint8_t index{0U}; index < 6U; ++index)
        {
            const float expected{ford.fraction[(index + rotation) % 6U]};
            passed = passed && expect(isNear(slowOut[index], expected, 0.002F), "slow gap fraction");
            passed = passed && expect(isNear(fastOut[index], expected, 0.002F), "fast gap fraction");
        }
    }

    // One magnet: the single gap is the whole revolution.
    const std::int64_t oneTime[1]{1'234'567};
    float oneOut[1]{};
    passed = passed
        && expect(driver::odometer::gapFractions(oneTime, 1U, 200'000, oneOut), "one magnet should derive")
        && expect(isNear(oneOut[0], 1.0F), "one magnet gives one whole-revolution gap");

    // Rubbish in.
    std::int64_t good[6]{};
    simulateRevolution(ford, slowUs, 0U, 0, good);
    float out[6]{};
    passed = passed
        && expect(!driver::odometer::gapFractions(nullptr, 6U, slowUs, out), "null timestamps rejected")
        && expect(!driver::odometer::gapFractions(good, 6U, slowUs, nullptr), "null output rejected")
        && expect(!driver::odometer::gapFractions(good, 0U, slowUs, out), "zero count rejected")
        && expect(!driver::odometer::gapFractions(good, 6U, 0, out), "zero revolution rejected")
        && expect(!driver::odometer::gapFractions(good, 6U, -5, out), "negative revolution rejected");

    // A revolution shorter than the timestamps it supposedly contains is impossible.
    passed = passed
        && expect(!driver::odometer::gapFractions(good, 6U, 1'000, out), "too-short revolution rejected");

    // Timestamps that do not advance cannot be a revolution.
    std::int64_t stalled[6]{100, 200, 200, 400, 500, 600};
    passed = passed
        && expect(!driver::odometer::gapFractions(stalled, 6U, slowUs, out), "repeated timestamp rejected");

    return passed;
}

bool testPhasing() noexcept
{
    bool passed{true};
    const GapTable ford{fordTable()};

    // Every starting position must be recovered, and comfortably.
    for (std::uint8_t rotation{0U}; rotation < 6U; ++rotation)
    {
        float observed[6]{};
        for (std::uint8_t index{0U}; index < 6U; ++index)
        {
            observed[index] = ford.fraction[(index + rotation) % 6U];
        }

        float margin{0.0F};
        const int found{driver::odometer::bestRotation(observed, ford, margin)};
        passed = passed && expect(found == static_cast<int>(rotation), "phase should be recovered exactly");
        passed = passed && expect(margin > driver::odometer::MinPhaseMargin, "margin should clear the gate");
        passed = passed && expect(isNear(margin, 0.25F, 0.01F), "Ford's layout should score about 0.25");
    }

    // The point of the margin gate: evenly spaced magnets cannot be phased at all,
    // every rotation fits equally, and the driver must not pretend otherwise.
    const GapTable uniform{driver::odometer::uniformTable(6U)};
    float uniformObserved[6]{};
    for (std::uint8_t index{0U}; index < 6U; ++index) { uniformObserved[index] = 1.0F / 6.0F; }

    float uniformMargin{1.0F};
    const int uniformFound{driver::odometer::bestRotation(uniformObserved, uniform, uniformMargin)};
    passed = passed && expect(uniformFound >= 0, "a uniform table is still a table");
    passed = passed
        && expect(uniformMargin < driver::odometer::MinPhaseMargin,
                  "a uniform wheel must fail the margin gate");

    // Noise of a few degrees must not move the answer.
    const float nudged[6]{0.131F, 0.119F, 0.244F, 0.256F, 0.122F, 0.128F};
    float nudgedMargin{0.0F};
    passed = passed
        && expect(driver::odometer::bestRotation(nudged, ford, nudgedMargin) == 0,
                  "small placement error should not change the phase")
        && expect(nudgedMargin > driver::odometer::MinPhaseMargin, "nudged margin should still clear the gate");

    float margin{0.0F};
    passed = passed
        && expect(driver::odometer::bestRotation(nullptr, ford, margin) == -1, "null observation rejected")
        && expect(driver::odometer::bestRotation(nudged, GapTable{}, margin) == -1, "empty stored table rejected");

    return passed;
}

bool testAgreementAndMean() noexcept
{
    bool passed{true};

    // Three measurements of the same wheel at different speeds, differing only a little.
    GapTable measured[3]{fordTable(), fordTable(), fordTable()};
    measured[0].fraction[0] = 0.127F;
    measured[0].fraction[1] = 0.123F;
    measured[1].fraction[0] = 0.124F;
    measured[1].fraction[1] = 0.126F;

    std::uint8_t worstGap{0U};
    float worstSpread{0.0F};
    passed = passed
        && expect(driver::odometer::tablesAgree(measured, 3U, 0.01F, worstGap, worstSpread),
                  "small differences across speeds should agree")
        && expect(worstSpread < 0.01F, "spread should be small");

    // One gap that moves with speed is the motor's roughness, not the wheel's shape.
    GapTable contaminated[3]{fordTable(), fordTable(), fordTable()};
    contaminated[0].fraction[2] = 0.20F;
    contaminated[0].fraction[3] = 0.30F;
    contaminated[2].fraction[2] = 0.30F;
    contaminated[2].fraction[3] = 0.20F;

    passed = passed
        && expect(!driver::odometer::tablesAgree(contaminated, 3U, 0.01F, worstGap, worstSpread),
                  "a gap that changes with speed must be refused")
        && expect((worstGap == 2U) || (worstGap == 3U), "the disagreeing gap should be named")
        && expect(isNear(worstSpread, 0.10F, 0.001F), "spread should be reported");

    passed = passed
        && expect(!driver::odometer::tablesAgree(measured, 1U, 0.01F, worstGap, worstSpread),
                  "one table cannot agree with itself")
        && expect(!driver::odometer::tablesAgree(nullptr, 3U, 0.01F, worstGap, worstSpread),
                  "null tables rejected");

    // Tables of different wheels must not be compared.
    GapTable mismatched[2]{fordTable(), driver::odometer::uniformTable(4U)};
    passed = passed
        && expect(!driver::odometer::tablesAgree(mismatched, 2U, 0.5F, worstGap, worstSpread),
                  "different magnet counts should be refused");

    GapTable mean{};
    passed = passed
        && expect(driver::odometer::meanTable(measured, 3U, mean), "mean of three tables")
        && expect(driver::odometer::isPlausible(mean), "the mean must still sum to 1")
        && expect(mean.count == 6U, "the mean keeps the magnet count")
        && expect(isNear(mean.fraction[2], 0.25F, 0.002F), "the wide gap should average to a quarter");

    float total{0.0F};
    for (std::uint8_t index{0U}; index < mean.count; ++index) { total += mean.fraction[index]; }
    passed = passed && expect(isNear(total, 1.0F), "the mean should be renormalised to exactly 1");

    passed = passed
        && expect(!driver::odometer::meanTable(nullptr, 3U, mean), "null tables rejected")
        && expect(!driver::odometer::meanTable(measured, 0U, mean), "no tables rejected")
        && expect(!driver::odometer::meanTable(mismatched, 2U, mean), "mismatched counts rejected");

    return passed;
}

/**
 * @brief The whole chain, as the driver will run it.
 *
 * Simulate a wheel turning at one speed, derive the fractions from the timestamps, and
 * check the phase comes back - without telling the maths anything about where the wheel
 * started.
 */
bool testEndToEnd() noexcept
{
    bool passed{true};
    const GapTable ford{fordTable()};
    const std::int64_t revolutionUs{213'628}; // 0.5 m/s on a 34 mm wheel.

    for (std::uint8_t rotation{0U}; rotation < 6U; ++rotation)
    {
        std::int64_t times[6]{};
        simulateRevolution(ford, revolutionUs, rotation, 42'000'000, times);

        float observed[6]{};
        passed = passed
            && expect(driver::odometer::gapFractions(times, 6U, revolutionUs, observed),
                      "end to end: fractions should derive");

        float margin{0.0F};
        const int found{driver::odometer::bestRotation(observed, ford, margin)};
        passed = passed
            && expect(found == static_cast<int>(rotation), "end to end: phase should match the simulated start")
            && expect(margin > driver::odometer::MinPhaseMargin, "end to end: margin should clear the gate");

        // And the corrected per-gap speed must come out right where a nominal
        // sixth-of-a-turn would be 33 % wrong.
        const float circumferenceM{0.10681F};
        const std::uint8_t currentGap{static_cast<std::uint8_t>((5U + rotation) % 6U)};
        const std::int64_t lastGapUs{times[5] - times[4]};
        const float corrected{ford.fraction[currentGap] * circumferenceM
                              / (static_cast<float>(lastGapUs) * 1.0e-6F)};
        passed = passed && expect(isNear(corrected, 0.5F, 0.01F), "end to end: corrected speed should be 0.5 m/s");
    }

    return passed;
}
} // namespace

namespace test
{
bool runOdometerGapsTest() noexcept
{
    const bool passed{testUniformAndPlausibility() && testFractionsFromTimestamps() && testPhasing()
                      && testAgreementAndMean() && testEndToEnd()};

    if (passed) { std::printf("Odometer gaps test succeeded!\n"); }

    return passed;
}
} // namespace test
