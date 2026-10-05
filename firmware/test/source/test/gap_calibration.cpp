/**
 * @file gap_calibration.cpp
 * @brief Host tests for the magnet gap calibration state machine.
 *
 * This is the first time anything in this measurement path has been testable without the
 * car, a lifted wheel and a moved SPD/SCL wire. It is worth having because the sequencing
 * is where the mistakes are: three duties, a settle timer, a stall timer, a revolution
 * counter and four separate reasons to refuse a table. ADR 0009 records why it moved here.
 */
#include "test/gap_calibration.h"

#include <cmath>
#include <cstdio>
#include <limits>

#include "driver/odometer/stub.h"
#include "system/ford.h"
#include "system/logic/gapCalibration.h"

namespace
{
namespace ford = app::ford;
namespace cal = app::ford::calibration;
using app::logic::GapCalibration;
using driver::odometer::Stub;

// A simulated wheel of its own rather than Ford's: the state machine is the same for any
// wheel, and Ford's current magnets are evenly spaced, which can never be calibrated. This
// is Ford's earlier six-magnet layout, uneven enough to phase.
constexpr std::uint8_t Magnets{6U};
constexpr float Wheel[Magnets]{0.125F, 0.125F, 0.25F, 0.25F, 0.125F, 0.125F};
constexpr float NoSensor{std::numeric_limits<float>::quiet_NaN()};

bool fail(const char* what) noexcept
{
    std::printf("Gap calibration test failed: %s\n", what);
    return false;
}

Stub makeOdometer() noexcept
{
    return Stub{driver::odometer::Config{
        .pulsesPerRevolution = Magnets,
        .wheelDiameterM = static_cast<float>(ford::WheelDiameterM),
    }};
}

/** Walk the clock past the settle timer, which must not sample while it runs. */
bool settle(GapCalibration& calibration, Stub& odometer, std::uint32_t& nowMs) noexcept
{
    const auto startedAt = nowMs;
    while (calibration.phase() == GapCalibration::Phase::Settling)
    {
        if ((nowMs - startedAt) > (cal::SettleMs * 2U)) { return false; }
        nowMs += 10U;
        calibration.update(nowMs, true, NoSensor, &odometer);
    }
    return calibration.phase() == GapCalibration::Phase::Sampling;
}

/** One whole revolution: the magnets pass, and the driver reports what it timed. */
void revolution(Stub& odometer, const float* gaps) noexcept
{
    odometer.simulatePulses(Magnets);
    (void)odometer.simulateObservedGaps(gaps, Magnets);
}

/** Feed whole revolutions until the current duty is averaged. */
bool sampleOneDuty(GapCalibration& calibration,
                   Stub& odometer,
                   const float* gaps,
                   std::uint32_t& nowMs) noexcept
{
    for (std::uint8_t turn{0U}; turn < cal::Revolutions; ++turn)
    {
        revolution(odometer, gaps);
        nowMs += 10U;
        calibration.update(nowMs, true, NoSensor, &odometer);
    }
    return true;
}

/**
 * @brief A whole run over a wheel built to its design gaps reaches a confirmable table.
 *
 * Also checks the duty actually asked for at each stage, because the recipe's three
 * speeds are the entire reason the measurement can tell geometry from motor ripple.
 */
bool aCleanRunMeasuresTheWheel() noexcept
{
    auto odometer = makeOdometer();
    if (!odometer.init()) { return fail("could not start the stub odometer"); }

    GapCalibration calibration{};
    std::uint32_t nowMs{1000U};
    calibration.start(nowMs, Magnets);

    if (calibration.phase() != GapCalibration::Phase::Settling)
    {
        return fail("a started run should begin by settling");
    }

    for (std::uint8_t duty{0U}; duty < cal::DutyCount; ++duty)
    {
        if (calibration.dutyIndex() != duty) { return fail("the duties should be measured in order"); }

        // While settling, the wheel is already being driven at this duty.
        const float asked = calibration.update(nowMs, true, NoSensor, &odometer);
        if (asked != cal::Duties[duty]) { return fail("the wrong duty was asked for while settling"); }

        if (!settle(calibration, odometer, nowMs)) { return fail("the run never left settling"); }
        if (!sampleOneDuty(calibration, odometer, Wheel, nowMs))
        {
            return fail("sampling a duty failed");
        }
    }

    if (calibration.phase() != GapCalibration::Phase::Measured)
    {
        return fail("a clean run should end with a table waiting to be confirmed");
    }
    if (!calibration.hasTable()) { return fail("a measured run should have a table"); }
    if (calibration.isStored()) { return fail("nothing should be stored without the operator"); }
    if (calibration.update(nowMs + 10U, true, NoSensor, &odometer) != 0.0F)
    {
        return fail("a finished run should ask for no drive");
    }

    const auto& result = calibration.result();
    if (result.table.count != Magnets) { return fail("the table should cover every magnet"); }
    for (std::uint8_t gap{0U}; gap < Magnets; ++gap)
    {
        if (std::fabs(result.table.fraction[gap] - Wheel[gap]) > 0.0005F)
        {
            return fail("the measured table should match the wheel it measured");
        }
    }
    if (result.spread > 0.0005F) { return fail("identical speeds should spread by nothing"); }
    if (result.margin < driver::odometer::MinPhaseMargin)
    {
        return fail("the design gaps should be phaseable");
    }

    calibration.markStored();
    if (!calibration.isStored()) { return fail("markStored should record the save"); }
    return true;
}

/** A wheel that stops turning mid-run is refused, and says the wheel stalled. */
bool aStalledWheelIsRefused() noexcept
{
    auto odometer = makeOdometer();
    if (!odometer.init()) { return fail("could not start the stub odometer"); }

    GapCalibration calibration{};
    std::uint32_t nowMs{1000U};
    calibration.start(nowMs, Magnets);
    if (!settle(calibration, odometer, nowMs)) { return fail("the run never left settling"); }

    // A few good revolutions, then the wheel stops while the motor is still driven.
    for (std::uint8_t turn{0U}; turn < 3U; ++turn)
    {
        revolution(odometer, Wheel);
        nowMs += 10U;
        calibration.update(nowMs, true, NoSensor, &odometer);
    }
    if (!calibration.isRunning()) { return fail("three revolutions should not end a run"); }

    nowMs += cal::StallMs + 1U;
    calibration.update(nowMs, true, NoSensor, &odometer);

    if (calibration.phase() != GapCalibration::Phase::Failed) { return fail("a stall should fail the run"); }
    if (calibration.failure() != GapCalibration::Failure::Stalled)
    {
        return fail("a stall should be reported as a stall");
    }
    if (calibration.hasTable()) { return fail("a stalled run must leave no table"); }
    return true;
}

/** The overheat guard stops a run; a missing sensor leaves the guard off instead. */
bool theOverheatGuardStopsARunWhenThereIsASensor() noexcept
{
    auto odometer = makeOdometer();
    if (!odometer.init()) { return fail("could not start the stub odometer"); }

    GapCalibration hot{};
    std::uint32_t nowMs{1000U};
    hot.start(nowMs, Magnets);
    nowMs += 10U;
    hot.update(nowMs, true, ford::MaxMotorTempC + 1.0F, &odometer);
    if (hot.phase() != GapCalibration::Phase::Failed) { return fail("a hot motor should fail the run"); }
    if (hot.failure() != GapCalibration::Failure::TooHot)
    {
        return fail("a hot motor should be reported as too hot");
    }

    // Exactly at the limit is not above it.
    GapCalibration warm{};
    nowMs = 1000U;
    warm.start(nowMs, Magnets);
    nowMs += 10U;
    warm.update(nowMs, true, ford::MaxMotorTempC, &odometer);
    if (!warm.isRunning()) { return fail("the limit itself should not stop a run"); }

    // No sensor reads NaN, and then the run proceeds without the guard. ADR 0009.
    GapCalibration blind{};
    nowMs = 1000U;
    blind.start(nowMs, Magnets);
    nowMs += 10U;
    blind.update(nowMs, true, NoSensor, &odometer);
    if (!blind.isRunning()) { return fail("a missing sensor should not refuse the run"); }
    return true;
}

/** Disarming mid-run ends it, and a part-measured wheel stores nothing. */
bool disarmingMidRunLeavesNothing() noexcept
{
    auto odometer = makeOdometer();
    if (!odometer.init()) { return fail("could not start the stub odometer"); }

    GapCalibration calibration{};
    std::uint32_t nowMs{1000U};
    calibration.start(nowMs, Magnets);
    if (!settle(calibration, odometer, nowMs)) { return fail("the run never left settling"); }

    // Two whole duties' worth of good revolutions would still not be a calibration.
    for (std::uint8_t turn{0U}; turn < cal::Revolutions; ++turn)
    {
        revolution(odometer, Wheel);
        nowMs += 10U;
        calibration.update(nowMs, true, NoSensor, &odometer);
    }
    if (calibration.dutyIndex() == 0U) { return fail("a finished duty should advance the index"); }

    nowMs += 10U;
    if (calibration.update(nowMs, false, NoSensor, &odometer) != 0.0F)
    {
        return fail("a disarmed run should ask for no drive");
    }
    if (calibration.phase() != GapCalibration::Phase::Failed)
    {
        return fail("disarming should end the run");
    }
    if (calibration.failure() != GapCalibration::Failure::Stopped)
    {
        return fail("disarming should be reported as stopped");
    }
    if (calibration.hasTable()) { return fail("an interrupted run must leave no table"); }
    return true;
}

/**
 * @brief Gaps that move with speed are refused.
 *
 * This is the check the whole three-speed recipe exists for: real geometry does not change
 * with speed, so a gap that does is the motor's torque ripple. Storing it would label
 * cogging as wheel geometry, which ADR 0008 calls worse than no table at all.
 */
bool gapsThatMoveWithSpeedAreRefused() noexcept
{
    auto odometer = makeOdometer();
    if (!odometer.init()) { return fail("could not start the stub odometer"); }

    // The third speed sees two gaps shifted well past the tolerance, in opposite
    // directions so the table still sums to one and stays plausible on its own.
    float drifted[driver::odometer::MaxPulsesPerRevolution]{};
    for (std::uint8_t gap{0U}; gap < Magnets; ++gap) { drifted[gap] = Wheel[gap]; }
    drifted[0] += cal::Tolerance * 4.0F;
    drifted[1] -= cal::Tolerance * 4.0F;

    GapCalibration calibration{};
    std::uint32_t nowMs{1000U};
    calibration.start(nowMs, Magnets);

    for (std::uint8_t duty{0U}; duty < cal::DutyCount; ++duty)
    {
        if (!settle(calibration, odometer, nowMs)) { return fail("the run never left settling"); }
        const float* gaps = (duty + 1U == cal::DutyCount) ? drifted : Wheel;
        if (!sampleOneDuty(calibration, odometer, gaps, nowMs)) { return fail("sampling failed"); }
    }

    if (calibration.phase() != GapCalibration::Phase::Failed)
    {
        return fail("disagreeing speeds should fail the run");
    }
    if (calibration.failure() != GapCalibration::Failure::Disagreed)
    {
        return fail("disagreeing speeds should be reported as a disagreement");
    }
    if (calibration.hasTable()) { return fail("a disagreeing run must leave no table"); }
    if (calibration.result().spread <= cal::Tolerance)
    {
        return fail("the reported spread should exceed the tolerance it broke");
    }
    return true;
}

/** Starting a run clears whatever was waiting, before the new run has measured anything. */
bool startingARunClearsThePendingTable() noexcept
{
    auto odometer = makeOdometer();
    if (!odometer.init()) { return fail("could not start the stub odometer"); }

    GapCalibration calibration{};
    std::uint32_t nowMs{1000U};
    calibration.start(nowMs, Magnets);
    for (std::uint8_t duty{0U}; duty < cal::DutyCount; ++duty)
    {
        if (!settle(calibration, odometer, nowMs)) { return fail("the run never left settling"); }
        if (!sampleOneDuty(calibration, odometer, Wheel, nowMs))
        {
            return fail("sampling failed");
        }
    }
    if (!calibration.hasTable()) { return fail("the first run should produce a table"); }

    calibration.start(nowMs, Magnets);
    if (calibration.hasTable()) { return fail("starting again should clear the pending table"); }
    if (calibration.isStored()) { return fail("starting again should clear the stored flag"); }
    if (calibration.dutyIndex() != 0U) { return fail("starting again should restart at the first duty"); }

    // And a re-run that is interrupted leaves nothing at all, by design (ADR 0009).
    nowMs += 10U;
    calibration.update(nowMs, false, NoSensor, &odometer);
    if (calibration.hasTable()) { return fail("an interrupted re-run must not resurrect a table"); }
    return true;
}

/** Without an Odometer there is nothing to measure, and the run says so. */
bool withoutAnOdometerThereIsNothingToMeasure() noexcept
{
    GapCalibration calibration{};
    std::uint32_t nowMs{1000U};
    calibration.start(nowMs, Magnets);
    nowMs += 10U;
    if (calibration.update(nowMs, true, NoSensor, nullptr) != 0.0F)
    {
        return fail("a run with no odometer should ask for no drive");
    }
    if (calibration.failure() != GapCalibration::Failure::NoOdometer)
    {
        return fail("a missing odometer should be reported");
    }

    // An uninitialized odometer is no better than none.
    auto odometer = makeOdometer();
    GapCalibration second{};
    nowMs = 1000U;
    second.start(nowMs, Magnets);
    nowMs += 10U;
    second.update(nowMs, true, NoSensor, &odometer);
    if (second.failure() != GapCalibration::Failure::NoOdometer)
    {
        return fail("an uninitialized odometer should be reported as missing");
    }
    return true;
}

/** Ford's compiled-in wheel has to be a wheel: its gaps are fractions that sum to one. */
bool theDesignGapsDescribeAWholeWheel() noexcept
{
    float sum{0.0F};
    for (std::uint8_t gap{0U}; gap < ford::OdometerMagnets; ++gap)
    {
        if (ford::DesignGapFractions[gap] <= 0.0F) { return fail("a design gap should be positive"); }
        sum += ford::DesignGapFractions[gap];
    }
    if (std::fabs(sum - 1.0F) > 1e-6F) { return fail("the design gaps should sum to one"); }
    if (cal::DutyCount < 2U)
    {
        return fail("the recipe needs at least two speeds to tell geometry from ripple");
    }
    return true;
}
} // namespace

namespace test
{
bool runGapCalibrationTest() noexcept
{
    if (!theDesignGapsDescribeAWholeWheel()) { return false; }
    if (!aCleanRunMeasuresTheWheel()) { return false; }
    if (!aStalledWheelIsRefused()) { return false; }
    if (!theOverheatGuardStopsARunWhenThereIsASensor()) { return false; }
    if (!disarmingMidRunLeavesNothing()) { return false; }
    if (!gapsThatMoveWithSpeedAreRefused()) { return false; }
    if (!startingARunClearsThePendingTable()) { return false; }
    if (!withoutAnOdometerThereIsNothingToMeasure()) { return false; }

    std::printf("Gap calibration test succeeded!\n");
    return true;
}
} // namespace test
