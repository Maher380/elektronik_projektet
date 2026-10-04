/**
 * @file gapCalibration.cpp
 * @brief The magnet gap calibration state machine.
 *
 * The sequence is the one the A89301 configuration app's `cal` command established and
 * measured on the car: hold each duty of the recipe, let the speed steady, then average
 * whole revolutions. What changed is that it yields between ticks instead of blocking, so
 * the shared loop keeps the operator's heartbeat alive and the run stays interruptible.
 */
#include "system/logic/gapCalibration.h"

#include <cmath>

#include "driver/odometer/interface.h"

namespace app::logic
{
namespace
{
namespace cal = app::ford::calibration;
namespace ford = app::ford;
} // namespace

void GapCalibration::start(const std::uint32_t nowMs, const std::uint8_t magnets) noexcept
{
    // Clear the previous run before this one has produced anything, so what the page shows
    // always belongs to the most recent run.
    myResult = {};
    myFailure = Failure::None;
    myStored = false;
    myMagnets = magnets;
    myDutyIndex = 0U;

    for (auto& table : myMeasured) { table = {}; }

    if ((magnets == 0U) || (magnets > driver::odometer::MaxPulsesPerRevolution))
    {
        myPhase = Phase::Failed;
        myFailure = Failure::NoOdometer;
        return;
    }

    beginSettling(nowMs);
}

void GapCalibration::abandon(const Failure why) noexcept
{
    if (!isRunning()) { return; }

    myPhase = Phase::Failed;
    myFailure = why;
}

void GapCalibration::beginSettling(const std::uint32_t nowMs) noexcept
{
    myPhase = Phase::Settling;
    myPhaseStartMs = nowMs;
    mySamples = 0U;
    for (auto& sum : mySums) { sum = 0.0; }
}

float GapCalibration::update(const std::uint32_t nowMs,
                             const bool armed,
                             const float motorTemperatureC,
                             driver::odometer::Interface* const odometer) noexcept
{
    if (!isRunning()) { return 0.0F; }

    // A run cannot outlive its arming: an operator stop, a lost connection and a stale
    // heartbeat all arrive here as a disarm, and all of them mean nothing is stored.
    if (!armed)
    {
        abandon(Failure::Stopped);
        return 0.0F;
    }
    if ((odometer == nullptr) || !odometer->isInitialized())
    {
        abandon(Failure::NoOdometer);
        return 0.0F;
    }
    // A missing or failed sensor reads NaN, and then the guard is simply off. That is a
    // decision, not an oversight; ADR 0009 records why and what it costs.
    if (std::isfinite(motorTemperatureC) && (motorTemperatureC > ford::MaxMotorTempC))
    {
        abandon(Failure::TooHot);
        return 0.0F;
    }

    if (myPhase == Phase::Settling)
    {
        if ((nowMs - myPhaseStartMs) >= cal::SettleMs)
        {
            myPhase = Phase::Sampling;
            myLastPulses = odometer->pulseCount();
            myLastSampleMs = nowMs;
        }
        return cal::Duties[myDutyIndex];
    }

    // Sampling.
    if ((nowMs - myLastSampleMs) > cal::StallMs)
    {
        abandon(Failure::Stalled);
        return 0.0F;
    }

    // One sample per whole revolution. The driver's window slides by one magnet per pulse,
    // so sampling faster would average overlapping windows and only look like more data
    // than it is.
    const std::uint32_t pulses{odometer->pulseCount()};
    if ((pulses - myLastPulses) >= myMagnets)
    {
        float gaps[driver::odometer::MaxPulsesPerRevolution]{};
        if (odometer->observedGaps(gaps, myMagnets))
        {
            for (std::uint8_t gap{0U}; gap < myMagnets; ++gap)
            {
                mySums[gap] += static_cast<double>(gaps[gap]);
            }
            ++mySamples;
        }
        // Reset whether or not the gaps could be read: a revolution did pass, so the wheel
        // is turning and the stall timer has been answered.
        myLastPulses = pulses;
        myLastSampleMs = nowMs;
    }

    if (mySamples >= cal::Revolutions)
    {
        finishDuty(nowMs);
        return isRunning() ? cal::Duties[myDutyIndex] : 0.0F;
    }

    return cal::Duties[myDutyIndex];
}

void GapCalibration::finishDuty(const std::uint32_t nowMs) noexcept
{
    auto& table = myMeasured[myDutyIndex];
    table.count = myMagnets;
    for (std::uint8_t gap{0U}; gap < myMagnets; ++gap)
    {
        table.fraction[gap] = static_cast<float>(mySums[gap] / mySamples);
    }

    ++myDutyIndex;
    if (myDutyIndex >= cal::DutyCount)
    {
        concludeRun();
        return;
    }
    beginSettling(nowMs);
}

void GapCalibration::concludeRun() noexcept
{
    namespace odo = driver::odometer;

    std::uint8_t worstGap{0U};
    float worstSpread{0.0F};
    if (!odo::tablesAgree(myMeasured, cal::DutyCount, cal::Tolerance, worstGap, worstSpread))
    {
        // A gap that changes with speed is the motor, not the wheel.
        myResult.worstGap = worstGap;
        myResult.spread = worstSpread;
        myPhase = Phase::Failed;
        myFailure = Failure::Disagreed;
        return;
    }

    odo::GapTable mean{};
    if (!odo::meanTable(myMeasured, cal::DutyCount, mean))
    {
        myPhase = Phase::Failed;
        myFailure = Failure::NotPlausible;
        return;
    }

    // Can this wheel be phased at all? Matching the table against itself gives the distance
    // to the next-best rotation, which is exactly what the driver has to clear at run time.
    // Magnets too close to evenly spaced score 0 and can never be placed, so the table would
    // be stored and then never used.
    float margin{0.0F};
    (void)odo::bestRotation(mean.fraction, mean, margin);

    myResult.table = mean;
    myResult.spread = worstSpread;
    myResult.margin = margin;
    myResult.worstGap = worstGap;

    if (margin < odo::MinPhaseMargin)
    {
        myPhase = Phase::Failed;
        myFailure = Failure::ThinMargin;
        return;
    }

    myPhase = Phase::Measured;
}

} // namespace app::logic
