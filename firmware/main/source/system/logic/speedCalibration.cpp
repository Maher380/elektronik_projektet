/**
 * @file speedCalibration.cpp
 * @brief The speed calibration state machine.
 *
 * Each leg is driven, then braked, then the next leg starts in the other direction. The
 * direction-change brake in FordLogic::executeAction() still applies below this, so the
 * BrakeMs pause here is extra time for the car to stand still, not the only protection.
 */
#include "system/logic/speedCalibration.h"

#include <algorithm>
#include <cmath>

#include "driver/odometer/interface.h"

namespace app::logic
{
namespace
{
namespace spd = app::ford::speed_calibration;
namespace ford = app::ford;

/** Linear interpolation through (xs, ys), extended along the end segments. */
float interpolate(const float* xs, const float* ys, const std::uint8_t count, const float x) noexcept
{
    if (count == 1U) { return ys[0]; }

    std::uint8_t segment{0U};
    while ((segment < (count - 2U)) && (x > xs[segment + 1U])) { ++segment; }

    const float span = xs[segment + 1U] - xs[segment];
    if (span <= 0.0F) { return ys[segment]; }
    return ys[segment] + ((x - xs[segment]) / span) * (ys[segment + 1U] - ys[segment]);
}

/** Row of the learned feed-forward for a direction. */
std::uint8_t rowOf(const bool forward) noexcept { return forward ? 0U : 1U; }

constexpr float NaN{std::numeric_limits<float>::quiet_NaN()};
} // namespace

SpeedCalibration::SpeedCalibration() noexcept
{
    // Until a leg has measured it, the feed-forward is read off the first floor run.
    for (std::uint8_t index{0U}; index < spd::TargetCount; ++index)
    {
        const float duty = interpolate(spd::InitialSpeedsMs, spd::InitialDuties, spd::InitialCount,
                                       spd::TargetsMs[index]);
        myFeedForward[0][index] = duty;
        myFeedForward[1][index] = duty;
    }
}

SpeedCalibration::Plan SpeedCalibration::planOf(const std::uint8_t leg) noexcept
{
    // Even legs drive forward and odd legs back, so the car shuttles between two marks.
    constexpr std::uint8_t targetLegs{static_cast<std::uint8_t>(spd::TargetCount * 2U)};
    if (leg < targetLegs) { return Plan{0.0F, spd::TargetsMs[leg / 2U], (leg % 2U) == 0U, false}; }
    if (leg < (targetLegs + 2U))
    {
        const bool up = leg == targetLegs;
        return Plan{up ? spd::StepLowMs : spd::StepHighMs, up ? spd::StepHighMs : spd::StepLowMs, up, false};
    }
    return Plan{0.0F, 0.0F, leg == (targetLegs + 2U), true};
}

std::uint8_t SpeedCalibration::targetIndexOf(const float speedMs) noexcept
{
    for (std::uint8_t index{0U}; index < spd::TargetCount; ++index)
    {
        if (std::fabs(spd::TargetsMs[index] - speedMs) < 0.001F) { return index; }
    }
    return spd::TargetCount;
}

float SpeedCalibration::feedForward(const float speedMs, const bool forward) const noexcept
{
    const float duty = interpolate(spd::TargetsMs, myFeedForward[rowOf(forward)], spd::TargetCount, speedMs);
    return std::clamp(duty, spd::MinDuty, spd::MaxDuty);
}

float SpeedCalibration::metersPerPulse() const noexcept
{
    return myCircumferenceM / static_cast<float>(myMagnets);
}

void SpeedCalibration::start(const std::uint32_t nowMs,
                             const std::uint8_t magnets,
                             const float circumferenceM) noexcept
{
    // The legs are forgotten; the learned feed-forward and stopping factor are kept.
    myFailure = Failure::None;
    myHasLastLeg = false;
    myLastLeg = {};
    myLegIndex = 0U;
    myMagnets = magnets;
    myCircumferenceM = circumferenceM;

    if ((magnets == 0U) || !(circumferenceM > 0.0F))
    {
        myPhase = Phase::Failed;
        myFailure = Failure::NoOdometer;
        return;
    }

    beginLeg(nowMs, 0U);
    myLegStartPending = true;
}

void SpeedCalibration::abandon(const Failure why) noexcept
{
    if (!isRunning()) { return; }

    myPhase = Phase::Failed;
    myFailure = why;
}

void SpeedCalibration::beginLeg(const std::uint32_t nowMs, const std::uint32_t pulses) noexcept
{
    myPhase = Phase::Driving;
    myPlan = planOf(myLegIndex);
    myLegStartMs = nowMs;
    myLegStartPulses = pulses;
    myLastTickMs = nowMs;

    myLastPulses = pulses;
    myLastEdgeMs = nowMs;
    myMoved = false;

    myIntegral = 0.0F;
    myLastDuty = 0.0F;
    myStepped = false;
    myReached = false;
    myFinalReached = false;
    myOvershootMs = 0.0F;

    myWindowStarted = false;
    myDutySum = 0.0F;
    myDutyMs = 0U;

    myMinimumStep = -1;
    myStepHasEdge = false;
    myLowestDuty = NaN;
    myLowestSpeedMs = NaN;

    myLeg = {};
    myLeg.index = myLegIndex;
    myLeg.plan = myPlan;
}

float SpeedCalibration::targetDuty(const std::uint32_t nowMs, const float speedMs) noexcept
{
    const bool holdingLast = (myPlan.fromMs <= 0.0F) || myStepped;
    const float target = holdingLast ? myPlan.targetMs : myPlan.fromMs;

    if (!myReached && (std::fabs(speedMs - target) <= (spd::ReachedBand * target)))
    {
        myReached = true;
        myReachedMs = nowMs;
        if (holdingLast)
        {
            myFinalReached = true;
            myLeg.riseS = static_cast<float>(nowMs - myRiseFromMs) / 1000.0F;
        }
    }

    // Overshoot: how far past the target the speed went, in the direction it came from.
    if (myFinalReached)
    {
        const bool rising = myPlan.targetMs > myPlan.fromMs;
        myOvershootMs = std::max(myOvershootMs, rising ? (speedMs - target) : (target - speedMs));
    }

    // A Step leg moves on to its second target once the first has been held.
    if (!holdingLast && myReached && ((nowMs - myReachedMs) >= spd::HoldMs))
    {
        myStepped = true;
        myReached = false;
        myRiseFromMs = nowMs;
        myIntegral = 0.0F;
        return targetDuty(nowMs, speedMs);
    }

    // PI around the feed-forward. The integral only runs near the target, so the climb from
    // standstill does not wind it up into an overshoot.
    const float error = target - speedMs;
    const std::uint32_t dtMs = nowMs - myLastTickMs;
    if (std::fabs(error) <= (0.2F * target))
    {
        myIntegral += spd::Ki * error * static_cast<float>(dtMs) / 1000.0F;
        myIntegral = std::clamp(myIntegral, -spd::IntegralLimit, spd::IntegralLimit);
    }
    const float duty = feedForward(target, myPlan.forward) + (spd::Kp * error) + myIntegral;
    return std::clamp(duty, spd::MinDuty, spd::MaxDuty);
}

float SpeedCalibration::minimumDuty(const std::uint32_t nowMs,
                                    const std::uint32_t pulses,
                                    const bool edge) noexcept
{
    if (myMinimumStep < 0)
    {
        if ((nowMs - myFirstEdgeMs) < spd::MinimumSettleMs) { return spd::StartDuty; }
        myMinimumStep = 0;
        myMinimumStepMs = nowMs;
        myStepHasEdge = false;
    }

    // Time the second half of each step, once the speed has had half a step to settle.
    const std::uint32_t inStep = nowMs - myMinimumStepMs;
    if (edge && (inStep >= (spd::MinimumStepMs / 2U)))
    {
        if (!myStepHasEdge)
        {
            myStepHasEdge = true;
            myStepFirstPulses = pulses;
            myStepFirstMs = nowMs;
        }
        myStepLastPulses = pulses;
        myStepLastMs = nowMs;
    }

    if (inStep >= spd::MinimumStepMs)
    {
        // The step rolled all the way through: it is the lowest so far.
        if (myStepHasEdge && (myStepLastPulses > myStepFirstPulses) && (myStepLastMs > myStepFirstMs))
        {
            myLowestDuty = spd::MinimumDuties[myMinimumStep];
            myLowestSpeedMs = static_cast<float>(myStepLastPulses - myStepFirstPulses) * metersPerPulse()
                * 1000.0F / static_cast<float>(myStepLastMs - myStepFirstMs);
        }
        ++myMinimumStep;
        myMinimumStepMs = nowMs;
        myStepHasEdge = false;
        if (myMinimumStep >= static_cast<std::int8_t>(spd::MinimumCount))
        {
            endLeg(nowMs, pulses, 0.0F, std::isfinite(myLowestDuty) ? Outcome::Bottom : Outcome::Stalled);
            return 0.0F;
        }
    }
    return spd::MinimumDuties[myMinimumStep];
}

void SpeedCalibration::endLeg(const std::uint32_t nowMs,
                              const std::uint32_t pulses,
                              const float speedMs,
                              Outcome outcome) noexcept
{
    myLeg.brakeSpeedMs = speedMs;

    if (outcome == Outcome::None)
    {
        // The car reached its mark (or the time limit): judge what it measured on the way.
        if (myPlan.minimum) { outcome = Outcome::Short; }
        else if (!myFinalReached)
        {
            outcome = Outcome::NotReached;
            myLeg.speedMs = speedMs;
        }
        else if (myWindowStarted && ((myWindowEndPulses - myWindowStartPulses) >= spd::MinMeasuredPulses)
                 && (myWindowEndMs > myWindowStartMs) && (myDutyMs > 0U))
        {
            outcome = Outcome::Reached;
            const float revolutions =
                static_cast<float>((myWindowEndPulses - myWindowStartPulses) / myMagnets);
            myLeg.speedMs = revolutions * myCircumferenceM * 1000.0F
                / static_cast<float>(myWindowEndMs - myWindowStartMs);
            myLeg.duty = myDutySum / static_cast<float>(myDutyMs);
        }
        else { outcome = Outcome::Short; }
    }

    if (myPlan.minimum && ((outcome == Outcome::Lowest) || (outcome == Outcome::Bottom) || (outcome == Outcome::Short)))
    {
        myLeg.duty = myLowestDuty;
        myLeg.speedMs = myLowestSpeedMs;
    }
    if (myFinalReached) { myLeg.overshootMs = std::max(0.0F, myOvershootMs); }

    myLeg.outcome = outcome;
    myPhase = Phase::Braking;
    myBrakeStartMs = nowMs;
    myBrakePulses = pulses;
}

void SpeedCalibration::finishLeg(const std::uint32_t nowMs, const std::uint32_t pulses) noexcept
{
    myLeg.stopM = static_cast<float>(pulses - myBrakePulses) * metersPerPulse();
    myLeg.distanceM = static_cast<float>(pulses - myLegStartPulses) * metersPerPulse();

    // Learn the stopping distance from every brake at a useful speed. Halfway between the
    // old value and the new sample, because one brake is only as good as a pulse or two.
    if (myLeg.brakeSpeedMs >= 0.3F)
    {
        const float sample = myLeg.stopM / (myLeg.brakeSpeedMs * myLeg.brakeSpeedMs);
        myStopK = 0.5F * (myStopK + std::clamp(sample, 0.02F, 1.0F));
    }

    // The duty that held a target is the best feed-forward for it next time.
    if (myLeg.outcome == Outcome::Reached)
    {
        const std::uint8_t index = targetIndexOf(myPlan.targetMs);
        if (index < spd::TargetCount) { myFeedForward[rowOf(myPlan.forward)][index] = myLeg.duty; }
    }

    myLastLeg = myLeg;
    myHasLastLeg = true;

    ++myLegIndex;
    if (myLegIndex >= spd::LegCount)
    {
        myPhase = Phase::Finished;
        return;
    }
    beginLeg(nowMs, pulses);
}

float SpeedCalibration::update(const std::uint32_t nowMs,
                               const bool armed,
                               const float motorTemperatureC,
                               driver::odometer::Interface* const odometer) noexcept
{
    if (!isRunning()) { return 0.0F; }

    // A run cannot outlive its arming: an operator stop, a lost connection and a stale
    // heartbeat all arrive here as a disarm.
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
    // A missing sensor reads NaN and then the guard is off, as for GapCalibration.
    if (std::isfinite(motorTemperatureC) && (motorTemperatureC > ford::MaxMotorTempC))
    {
        abandon(Failure::TooHot);
        return 0.0F;
    }

    const std::uint32_t pulses = odometer->pulseCount();
    const float speedMs = odometer->speed();
    if (myLegStartPending)
    {
        // start() had no odometer to read; the first leg begins counting from here.
        myLegStartPending = false;
        myLegStartPulses = pulses;
        myLastPulses = pulses;
    }

    if (myPhase == Phase::Braking)
    {
        if ((nowMs - myBrakeStartMs) >= spd::BrakeMs) { finishLeg(nowMs, pulses); }
        return 0.0F;
    }

    // Driving.
    const bool edge = pulses != myLastPulses;
    if (edge)
    {
        myLastPulses = pulses;
        myLastEdgeMs = nowMs;
        if (!myMoved)
        {
            myMoved = true;
            myFirstEdgeMs = nowMs;
            myRiseFromMs = nowMs;
        }
    }
    const float sign = myPlan.forward ? 1.0F : -1.0F;

    if (!myMoved)
    {
        if ((nowMs - myLegStartMs) >= spd::StartTimeoutMs)
        {
            endLeg(nowMs, pulses, 0.0F, Outcome::NoStart);
            return 0.0F;
        }
        // Start-up needs a firmer push than holding a speed does.
        myLastTickMs = nowMs;
        return sign * spd::StartDuty;
    }

    if ((nowMs - myLastEdgeMs) >= spd::StallMs)
    {
        const bool found = myPlan.minimum && std::isfinite(myLowestDuty);
        endLeg(nowMs, pulses, 0.0F, found ? Outcome::Lowest : Outcome::Stalled);
        return 0.0F;
    }

    // Brake when the distance left is what the car needs to stop, so it stops on the mark.
    const float travelledM = static_cast<float>(pulses - myLegStartPulses) * metersPerPulse();
    if (((travelledM + (myStopK * speedMs * speedMs)) >= spd::LegM)
        || ((nowMs - myLegStartMs) >= spd::MaxLegMs))
    {
        endLeg(nowMs, pulses, speedMs, Outcome::None);
        return 0.0F;
    }

    float duty{0.0F};
    if (myPlan.minimum) { duty = minimumDuty(nowMs, pulses, edge); }
    else
    {
        duty = targetDuty(nowMs, speedMs);

        // The steady window opens HoldMs after the final target is reached and closes on
        // whole revolutions, so the magnet spacing cancels out of its speed.
        if (myFinalReached && edge)
        {
            if (!myWindowStarted && ((nowMs - myReachedMs) >= spd::HoldMs))
            {
                myWindowStarted = true;
                myWindowStartPulses = pulses;
                myWindowStartMs = nowMs;
                myWindowEndPulses = pulses;
                myWindowEndMs = nowMs;
            }
            else if (myWindowStarted && (((pulses - myWindowStartPulses) % myMagnets) == 0U))
            {
                myWindowEndPulses = pulses;
                myWindowEndMs = nowMs;
            }
        }
        if (myWindowStarted)
        {
            myDutySum += myLastDuty * static_cast<float>(nowMs - myLastTickMs);
            myDutyMs += nowMs - myLastTickMs;
        }
    }

    myLastTickMs = nowMs;
    myLastDuty = duty;
    if (myPhase != Phase::Driving) { return 0.0F; }
    return sign * duty;
}

} // namespace app::logic
