#pragma once

#include <cstdint>
#include <limits>

namespace app::logic
{
class ReverseController
{
public:
    enum class Phase { Ready, Braking, Settling, Reversing, Recovering, Holding };

    Phase update(std::uint32_t nowMs, std::uint32_t sampleSequence,
                 bool escapeRequired, bool routeClear, bool steeringReady = true) noexcept
    {
        if (myPhase == Phase::Ready && escapeRequired)
        {
            myPhase = myPulses < MaxPulses ? Phase::Braking : Phase::Holding;
            myDeadlineMs = nowMs + BrakeMs;
            myLastSampleSequence = sampleSequence;
        }
        if (myPhase == Phase::Ready) { return myPhase; }
        if (sampleSequence != myLastSampleSequence)
        {
            myLastSampleSequence = sampleSequence;
            myClearSamples = routeClear ? (myClearSamples < RequiredSamples ? myClearSamples + 1U : myClearSamples) : 0U;
            // Intermediate clearance still needs recovery; it must not stall forever in Holding.
            myStuckSamples = !routeClear ? (myStuckSamples < RequiredSamples ? myStuckSamples + 1U : myStuckSamples) : 0U;
        }
        if ((myPhase == Phase::Braking || myPhase == Phase::Settling)
            && myClearSamples >= RequiredSamples)
        {
            resume();
            return myPhase;
        }
        if (myPhase == Phase::Braking && !before(nowMs, myDeadlineMs))
        {
            myPhase = Phase::Settling;
            myDeadlineMs = nowMs + SettleMs;
        }
        if (myPhase == Phase::Settling && !before(nowMs, myDeadlineMs) && steeringReady)
        {
            myPhase = Phase::Reversing;
            myPulseStartedMs = nowMs;
            myDeadlineMs = nowMs + PulseMs;
            ++myPulses;
        }
        if (myPhase == Phase::Reversing && (!before(nowMs, myDeadlineMs)
            || (nowMs - myPulseStartedMs >= MinimumPulseMs && myClearSamples >= RequiredSamples)))
        {
            myPhase = Phase::Recovering;
            myDeadlineMs = nowMs + SettleMs;
        }
        if (myPhase == Phase::Recovering && !before(nowMs, myDeadlineMs))
        {
            myPhase = Phase::Holding;
            myLastSampleSequence = sampleSequence;
            myClearSamples = 0U;
            myStuckSamples = 0U;
        }
        if (myPhase == Phase::Holding)
        {
            if (myClearSamples >= RequiredSamples)
            {
                resume();
            }
            else if (myStuckSamples >= RequiredSamples && myPulses < MaxPulses)
            {
                myClearSamples = 0U;
                myStuckSamples = 0U;
                myPhase = Phase::Braking;
                myDeadlineMs = nowMs + BrakeMs;
            }
        }
        return myPhase;
    }

    float reverseDuty(std::uint32_t nowMs) const noexcept
    {
        if (myPhase != Phase::Reversing) { return 0.0F; }
        const std::uint32_t elapsedMs = nowMs - myPulseStartedMs;
        if (elapsedMs >= RampMs) { return 1.0F; }
        return RampStartDuty + (1.0F - RampStartDuty)
            * static_cast<float>(elapsedMs) / static_cast<float>(RampMs);
    }

    Phase phase() const noexcept { return myPhase; }
    bool attemptsExhausted() const noexcept { return myPulses >= MaxPulses; }

    void hold(std::uint32_t sampleSequence) noexcept
    {
        myPhase = Phase::Holding;
        myLastSampleSequence = sampleSequence;
        myClearSamples = 0U;
        myStuckSamples = 0U;
    }

    void reset() noexcept
    {
        resume();
        myPulses = 0U;
    }

private:
    void resume() noexcept
    {
        myPhase = Phase::Ready;
        myClearSamples = 0U;
        myStuckSamples = 0U;
    }

    static constexpr std::uint32_t BrakeMs{80U};
    static constexpr std::uint32_t SettleMs{100U};
    static constexpr std::uint32_t PulseMs{300U};
    static constexpr std::uint32_t RampMs{100U};
    static constexpr std::uint32_t MinimumPulseMs{200U};
    static constexpr float RampStartDuty{0.4F};
    static constexpr std::uint32_t RequiredSamples{2U};
    static constexpr std::uint32_t MaxPulses{2U};

    static bool before(std::uint32_t nowMs, std::uint32_t deadlineMs) noexcept
    {
        const std::uint32_t remaining = deadlineMs - nowMs;
        return remaining > 0U
            && remaining < (std::numeric_limits<std::uint32_t>::max() / 2U);
    }

    Phase myPhase{Phase::Ready};
    std::uint32_t myDeadlineMs{0U};
    std::uint32_t myPulseStartedMs{0U};
    std::uint32_t myLastSampleSequence{0U};
    std::uint32_t myPulses{0U};
    std::uint32_t myClearSamples{0U};
    std::uint32_t myStuckSamples{0U};
};
} // namespace app::logic
