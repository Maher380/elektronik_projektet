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
                 bool blocked, bool forwardClear) noexcept
    {
        if (myPhase == Phase::Ready && blocked)
        {
            myPhase = Phase::Braking;
            myDeadlineMs = nowMs + BrakeMs;
        }
        if (myPhase == Phase::Braking && !before(nowMs, myDeadlineMs))
        {
            myPhase = Phase::Settling;
            myDeadlineMs = nowMs + SettleMs;
        }
        if (myPhase == Phase::Settling && !before(nowMs, myDeadlineMs))
        {
            myPhase = Phase::Reversing;
            myDeadlineMs = nowMs + PulseMs;
            ++myPulses;
        }
        if (myPhase == Phase::Reversing && !before(nowMs, myDeadlineMs))
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
        if (myPhase == Phase::Holding && sampleSequence != myLastSampleSequence)
        {
            myLastSampleSequence = sampleSequence;
            if (forwardClear)
            {
                ++myClearSamples;
                myStuckSamples = 0U;
                if (myClearSamples >= RequiredSamples) { reset(); }
            }
            else
            {
                ++myStuckSamples;
                myClearSamples = 0U;
                if (myStuckSamples >= RequiredSamples && myPulses < MaxPulses)
                {
                    myPhase = Phase::Braking;
                    myDeadlineMs = nowMs + BrakeMs;
                }
            }
        }
        return myPhase;
    }

    void reset() noexcept
    {
        myPhase = Phase::Ready;
        myPulses = 0U;
        myClearSamples = 0U;
        myStuckSamples = 0U;
    }

private:
    static constexpr std::uint32_t BrakeMs{80U};
    static constexpr std::uint32_t SettleMs{100U};
    static constexpr std::uint32_t PulseMs{300U};
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
    std::uint32_t myLastSampleSequence{0U};
    std::uint32_t myPulses{0U};
    std::uint32_t myClearSamples{0U};
    std::uint32_t myStuckSamples{0U};
};
} // namespace app::logic
