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
    portEXIT_CRITICAL(&myMux);

    // Need at least two pulses to know a period, and a recent one to be moving.
    if ((periodUs <= 0) || ((esp_timer_get_time() - lastPulseUs) > StandstillTimeoutUs))
    {
        return 0.0F;
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
    for (auto& pulseTimeUs : myPulseTimesUs) { pulseTimeUs = 0; }
    portEXIT_CRITICAL(&myMux);
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
        }

        // The slot for this pulse still holds the pulse one revolution back, so read it
        // before overwriting. The window slides by one magnet on every pulse.
        const auto magnets = static_cast<std::uint32_t>(self->myPulsesPerRevolution);
        const std::uint32_t slot{self->myWindowPulses % magnets};
        if (self->myWindowPulses >= magnets)
        {
            self->myRevolutionPeriodUs = nowUs - self->myPulseTimesUs[slot];
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

} // namespace driver::odometer
