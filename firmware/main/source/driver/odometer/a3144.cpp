#include <cstdint>

#include "esp_attr.h"
#include "esp_timer.h"

#include "driver/gpio/edge.h"
#include "driver/odometer/a3144.h"

namespace driver::odometer
{
namespace
{
/** Pulses closer together than this are treated as noise and ignored (1 ms -> max 1 kHz). */
constexpr std::int64_t MinPulseIntervalUs{1'000};

/** Speed reads as zero if no pulse has been seen for this long (1 s). */
constexpr std::int64_t StandstillTimeoutUs{1'000'000};

constexpr float UsToS{1.0e-6F};

/**
 * @brief Clamp a configured magnet count into the range the driver can store.
 *
 * @param[in] config Odometer configuration.
 * @return Magnet count, at least 1 and at most MaxPulsesPerRevolution.
 */
std::uint8_t magnetsOf(const Config& config) noexcept
{
    if (config.pulsesPerRevolution == 0U) { return 1U; }
    if (config.pulsesPerRevolution > MaxPulsesPerRevolution) { return MaxPulsesPerRevolution; }
    return config.pulsesPerRevolution;
}

} // namespace

// -----------------------------------------------------------------------------
A3144::A3144(gpio::Interface& gpio, const Config& config) noexcept
    : myGpio{gpio}
    , myDistancePerPulse{distancePerPulse(config)}
    , myDistancePerRevolution{distancePerRevolution(config)}
    , myPulsesPerRevolution{magnetsOf(config)}
    , myPulseCount{0U}
    , myWindowPulses{0U}
    , myLastPulseUs{0}
    , myPulsePeriodUs{0}
    , myPulseTimesUs{}
    , myRevolutionPeriodUs{0}
    , myRevolutionCount{0U}
    , myWindowRestarts{0U}
    , myGapTable{}
    , myPhaseOffset{0U}
    , myPhased{false}
    , myCandidateOffset{0U}
    , myCandidateRuns{0U}
    , myLastSeenRevolution{0U}
    , myLastSeenRestarts{0U}
    , myPhaseLossCount{0U}
    , myForward{true}
    , myInitialized{false}
{}

// -----------------------------------------------------------------------------
A3144::~A3144() noexcept
{
    if (myInitialized)
    {
        deinit();
    }
}

// -----------------------------------------------------------------------------
bool A3144::init() noexcept
{
    // Return false if the odometer is already initialized.
    if (myInitialized) { return false; }

    // Return false if the GPIO failed to initialize.
    if (!myGpio.isInitialized()) { return false; }

    reset();

    // A3144 output is open-collector and active-low: count the falling edge
    // that occurs when a magnet passes the sensor.
    if (!myGpio.enableInterrupt(gpio::Edge::Falling, onPulse, this)) { return false; }

    myInitialized = true;
    return true;
}

// -----------------------------------------------------------------------------
bool A3144::deinit() noexcept
{
    // Return false if init() never succeeded.
    if (!myInitialized) { return false; }

    myGpio.disableInterrupt();

    myInitialized = false;
    return true;
}

// -----------------------------------------------------------------------------
bool A3144::isInitialized() const noexcept
{
    return myInitialized;
}

// -----------------------------------------------------------------------------
std::uint32_t A3144::pulseCount() const noexcept
{
    if (!myInitialized) { return 0U; }

    portENTER_CRITICAL(&myMux);
    const std::uint32_t count{myPulseCount};
    portEXIT_CRITICAL(&myMux);

    return count;
}

// -----------------------------------------------------------------------------
float A3144::distance() const noexcept
{
    return static_cast<float>(pulseCount()) * myDistancePerPulse;
}

// -----------------------------------------------------------------------------
float A3144::speed() const noexcept
{
    if (!myInitialized) { return 0.0F; }

    portENTER_CRITICAL(&myMux);
    const std::int64_t lastPulseUs{myLastPulseUs};
    const std::int64_t periodUs{myPulsePeriodUs};
    const std::int64_t revolutionUs{myRevolutionPeriodUs};
    const std::uint32_t windowPulses{myWindowPulses};
    portEXIT_CRITICAL(&myMux);

    const std::int64_t sinceLastUs{esp_timer_get_time() - lastPulseUs};
    const SpeedSource source{sourceFor(sinceLastUs, periodUs, revolutionUs)};

    if (source == SpeedSource::None) { return 0.0F; }

    // The phase is held, so the gap just traversed is known and can be timed on its own.
    // The newest pulse is window pulse windowPulses - 1.
    if (source == SpeedSource::PerGap)
    {
        const std::uint32_t magnetCount{static_cast<std::uint32_t>(myPulsesPerRevolution)};
        const std::uint32_t gap{(windowPulses - 1U + myPhaseOffset) % magnetCount};
        const float gapDistance{myGapTable.fraction[gap] * myDistancePerRevolution};
        return gapDistance / (static_cast<float>(periodUs) * UsToS);
    }

    // One whole revolution covers the full circumference however the magnets are
    // spaced, so prefer it. Before the first revolution is complete, fall back to the
    // latest gap and the nominal per-pulse distance, which assumes even spacing.
    if (revolutionUs > 0)
    {
        return myDistancePerRevolution / (static_cast<float>(revolutionUs) * UsToS);
    }

    return myDistancePerPulse / (static_cast<float>(periodUs) * UsToS);
}

// -----------------------------------------------------------------------------
void A3144::reset() noexcept
{
    portENTER_CRITICAL(&myMux);
    myPulseCount         = 0U;
    myWindowPulses       = 0U;
    myLastPulseUs        = 0;
    myPulsePeriodUs      = 0;
    myRevolutionPeriodUs = 0;
    myRevolutionCount    = 0U;
    myWindowRestarts     = 0U;
    for (auto& pulseTimeUs : myPulseTimesUs) { pulseTimeUs = 0; }
    portEXIT_CRITICAL(&myMux);

    // Phase state is only ever touched from a task, so it needs no critical section.
    // The offset is relative to the pulse numbering that has just restarted, so it
    // cannot survive this.
    myPhased             = false;
    myPhaseOffset        = 0U;
    myCandidateOffset    = 0U;
    myCandidateRuns      = 0U;
    myLastSeenRevolution = 0U;
    myLastSeenRestarts   = 0U;
    myPhaseLossCount     = 0U;
}

// -----------------------------------------------------------------------------
void IRAM_ATTR A3144::onPulse(void* arg) noexcept
{
    auto* self = static_cast<A3144*>(arg);
    const std::int64_t nowUs{esp_timer_get_time()};

    portENTER_CRITICAL_ISR(&self->myMux);

    // Ignore glitches; the first pulse has no previous pulse to compare with.
    const bool hasPrevious{self->myPulseCount > 0U};
    const std::int64_t sinceLastUs{nowUs - self->myLastPulseUs};

    if (!hasPrevious || (sinceLastUs >= MinPulseIntervalUs))
    {
        // A gap this long means the wheel stood still. The timestamps in the window
        // describe the journey before the stop, so start the window again at this pulse
        // instead of averaging the standstill into the speed. Distance is untouched.
        const bool restarting{hasPrevious && (sinceLastUs > StandstillTimeoutUs)};
        if (restarting)
        {
            self->myWindowPulses       = 0U;
            self->myRevolutionPeriodUs = 0;
            ++self->myWindowRestarts;
        }

        // The slot for this pulse still holds the pulse one revolution back, so read it
        // before overwriting. The window slides by one magnet on every pulse.
        const auto magnets = static_cast<std::uint32_t>(self->myPulsesPerRevolution);
        const std::uint32_t slot{self->myWindowPulses % magnets};
        if (self->myWindowPulses >= magnets)
        {
            self->myRevolutionPeriodUs = nowUs - self->myPulseTimesUs[slot];
            ++self->myRevolutionCount;
        }
        self->myPulseTimesUs[slot] = nowUs;
        ++self->myWindowPulses;

        // One pulse is not enough to know a speed, whether it is the first ever or the
        // first after a stop.
        self->myPulsePeriodUs = (hasPrevious && !restarting) ? sinceLastUs : 0;
        self->myLastPulseUs   = nowUs;
        ++self->myPulseCount;
    }

    portEXIT_CRITICAL_ISR(&self->myMux);
}


// -----------------------------------------------------------------------------
std::uint8_t A3144::magnets() const noexcept
{
    return myPulsesPerRevolution;
}

// -----------------------------------------------------------------------------
std::uint32_t A3144::phaseLossCount() const noexcept
{
    return myPhaseLossCount;
}

// -----------------------------------------------------------------------------
SpeedSource A3144::sourceFor(const std::int64_t movingUs,
                             const std::int64_t periodUs,
                             const std::int64_t revolutionUs) const noexcept
{
    if (!myInitialized) { return SpeedSource::None; }

    // Two pulses are needed to know any period, and a recent one to be moving at all.
    if ((periodUs <= 0) || (movingUs > StandstillTimeoutUs)) { return SpeedSource::None; }

    // Going backwards the gaps are traversed in the opposite order, so a table built
    // forwards does not describe them. See ADR 0008: Ford's gap pattern is a palindrome,
    // so the phase cannot be re-derived in reverse either. The window is used instead.
    const bool usable{myPhased && myForward && (myGapTable.count == myPulsesPerRevolution)};
    if (usable && (revolutionUs > 0)) { return SpeedSource::PerGap; }

    return SpeedSource::Revolution;
}

// -----------------------------------------------------------------------------
SpeedSource A3144::speedSource() const noexcept
{
    if (!myInitialized) { return SpeedSource::None; }

    portENTER_CRITICAL(&myMux);
    const std::int64_t lastPulseUs{myLastPulseUs};
    const std::int64_t periodUs{myPulsePeriodUs};
    const std::int64_t revolutionUs{myRevolutionPeriodUs};
    portEXIT_CRITICAL(&myMux);

    return sourceFor(esp_timer_get_time() - lastPulseUs, periodUs, revolutionUs);
}

// -----------------------------------------------------------------------------
void A3144::dropPhase() noexcept
{
    if (myPhased) { ++myPhaseLossCount; }

    myPhased        = false;
    myCandidateRuns = 0U;
}

// -----------------------------------------------------------------------------
void A3144::setForward(const bool forward) noexcept
{
    if (forward != myForward) { dropPhase(); }

    myForward = forward;
}

// -----------------------------------------------------------------------------
bool A3144::setGapTable(const GapTable& table) noexcept
{
    if (!isPlausible(table) || (table.count != myPulsesPerRevolution)) { return false; }

    myGapTable = table;

    // Any phase held described the previous table.
    dropPhase();
    return true;
}

// -----------------------------------------------------------------------------
bool A3144::observedGaps(float* out, const std::uint8_t count) const noexcept
{
    if ((out == nullptr) || (count != myPulsesPerRevolution)) { return false; }

    std::int64_t times[MaxPulsesPerRevolution]{};
    portENTER_CRITICAL(&myMux);
    const std::uint32_t windowPulses{myWindowPulses};
    const std::int64_t revolutionUs{myRevolutionPeriodUs};
    for (std::uint8_t index{0U}; index < count; ++index) { times[index] = myPulseTimesUs[index]; }
    portEXIT_CRITICAL(&myMux);

    if (revolutionUs <= 0) { return false; }

    // Slot (windowPulses + j) % count holds the j-th oldest of the stored pulses.
    std::int64_t ordered[MaxPulsesPerRevolution]{};
    for (std::uint8_t index{0U}; index < count; ++index)
    {
        ordered[index] = times[(windowPulses + index) % count];
    }

    float byOrder[MaxPulsesPerRevolution]{};
    if (!gapFractions(ordered, count, revolutionUs, byOrder)) { return false; }

    // Report by slot, not by age: a slot holds the same magnet on every revolution, so
    // out[i] stays the same physical gap and a caller can average over many revolutions.
    for (std::uint8_t index{0U}; index < count; ++index)
    {
        out[(windowPulses + index) % count] = byOrder[index];
    }

    return true;
}

// -----------------------------------------------------------------------------
void A3144::update() noexcept
{
    if (!myInitialized) { return; }

    std::int64_t times[MaxPulsesPerRevolution]{};
    portENTER_CRITICAL(&myMux);
    const std::uint32_t revolutions{myRevolutionCount};
    const std::uint32_t restarts{myWindowRestarts};
    const std::uint32_t windowPulses{myWindowPulses};
    const std::int64_t revolutionUs{myRevolutionPeriodUs};
    for (std::uint8_t index{0U}; index < myPulsesPerRevolution; ++index)
    {
        times[index] = myPulseTimesUs[index];
    }
    portEXIT_CRITICAL(&myMux);

    // Standing still restarts the pulse numbering the offset is relative to, so a phase
    // cannot survive it.
    if (restarts != myLastSeenRestarts)
    {
        myLastSeenRestarts = restarts;
        dropPhase();
    }

    const std::uint8_t count{myPulsesPerRevolution};
    const std::uint32_t magnetCount{static_cast<std::uint32_t>(count)};

    // The revolution window slides by one magnet per pulse, so consecutive windows share
    // all but one of their gaps and are not independent evidence. Only look at windows a
    // whole revolution apart, which share none: that is what makes agreeing on the same
    // phase several times in a row meaningful rather than merely repeated.
    if (revolutions == 0U) { return; }
    if ((myLastSeenRevolution != 0U) && ((revolutions - myLastSeenRevolution) < magnetCount))
    {
        return;
    }
    myLastSeenRevolution = revolutions;

    if ((myGapTable.count != count) || (revolutionUs <= 0)) { return; }

    std::int64_t ordered[MaxPulsesPerRevolution]{};
    for (std::uint8_t index{0U}; index < count; ++index)
    {
        ordered[index] = times[(windowPulses + index) % count];
    }

    float observed[MaxPulsesPerRevolution]{};
    if (!gapFractions(ordered, count, revolutionUs, observed)) { dropPhase(); return; }

    float margin{0.0F};
    const int rotation{bestRotation(observed, myGapTable, margin)};

    // Too close to call. On a wheel whose magnets are evenly spaced this is always the
    // outcome, which is the intended behaviour: such a wheel cannot be phased at all.
    if ((rotation < 0) || (margin < MinPhaseMargin)) { dropPhase(); return; }

    // observed[j] matched fraction[(j + rotation) % count], and observed[j] is the gap
    // ending at pulse windowPulses - count + j, so the gap ending at any pulse w is
    // fraction[(w + offset) % count] with offset as below.
    const std::uint8_t candidate{static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(rotation) + magnetCount - (windowPulses % magnetCount))
        % magnetCount)};

    if ((myCandidateRuns > 0U) && (candidate == myCandidateOffset))
    {
        if (myCandidateRuns < PhaseLockRevolutions) { ++myCandidateRuns; }
    }
    else
    {
        // The answer changed, so whatever was held is no longer being confirmed.
        dropPhase();
        myCandidateOffset = candidate;
        myCandidateRuns   = 1U;
    }

    // One revolution can be flukes; several in a row agreeing cannot, because the noise
    // that would cause a fluke is independent from one revolution to the next.
    if (!myPhased && (myCandidateRuns >= PhaseLockRevolutions))
    {
        myPhaseOffset = myCandidateOffset;
        myPhased      = true;
    }
}

} // namespace driver::odometer
