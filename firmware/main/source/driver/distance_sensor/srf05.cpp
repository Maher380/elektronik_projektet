#include <cstdint>
#include <limits>

#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "driver/distance_sensor/srf05.h"
#include "driver/gpio/edge.h"

namespace driver::distance_sensor
{
namespace
{
/** Trigger pulse length; the SRF05 requires at least 10 us. */
constexpr std::uint32_t TriggerPulseUs{10U};

/** Minimum time between trigger pulses, lets echoes of the previous ping die out (50 ms). */
constexpr std::int64_t PingIntervalUs{50'000};

/** A measurement with no completed echo after this long is treated as "no echo" (40 ms).
 *  The SRF05 ends the echo pulse after about 30 ms if nothing is detected. */
constexpr std::int64_t EchoTimeoutUs{40'000};

/** Results older than this are reported as NaN (200 ms). */
constexpr std::int64_t StaleResultUs{200'000};

/** Echo pulse width per cm of distance: sound travels ~29 us/cm and the pulse covers the round trip. */
constexpr float UsPerCm{58.0F};

/** Valid measuring range in cm; readings outside are reported as NaN. */
constexpr float MinDistanceCm{2.0F};
constexpr float MaxDistanceCm{400.0F};

constexpr float Invalid{std::numeric_limits<float>::quiet_NaN()};

} // namespace

// -----------------------------------------------------------------------------
SRF05::SRF05(gpio::Interface& trigger, gpio::Interface& echo) noexcept
    : myTrigger{trigger}
    , myEcho{echo}
    , myTriggerUs{0}
    , myEchoStartUs{0}
    , myResultUs{0}
    , myEchoWidthUs{0}
    , myAwaitingEcho{false}
    , myEchoHigh{false}
    , myInitialized{false}
{
    // Return if any of the GPIOs failed to initialize.
    if (!myTrigger.isInitialized() || !myEcho.isInitialized()) { return; }

    myTrigger.write(false);

    // Timestamp both edges of the echo pulse.
    myInitialized = myEcho.enableInterrupt(gpio::Edge::Both, onEcho, this);
}

// -----------------------------------------------------------------------------
SRF05::~SRF05() noexcept
{
    if (myInitialized)
    {
        myEcho.disableInterrupt();
    }
}

// -----------------------------------------------------------------------------
float SRF05::readDistance() noexcept
{
    if (!myInitialized) { return Invalid; }

    const std::int64_t nowUs{esp_timer_get_time()};

    portENTER_CRITICAL(&myMux);

    // Give up on a measurement whose echo never completed.
    if (myAwaitingEcho && ((nowUs - myTriggerUs) >= EchoTimeoutUs))
    {
        myAwaitingEcho = false;
        myEchoHigh     = false;
        myEchoWidthUs  = 0;
        myResultUs     = nowUs;
    }

    const bool triggerDue{!myAwaitingEcho && ((nowUs - myTriggerUs) >= PingIntervalUs)};
    const std::int64_t resultUs{myResultUs};
    const std::int64_t echoWidthUs{myEchoWidthUs};

    portEXIT_CRITICAL(&myMux);

    if (triggerDue) { trigger(nowUs); }

    // No result yet, no echo, or the result is too old.
    if ((resultUs == 0) || (echoWidthUs <= 0) || ((nowUs - resultUs) > StaleResultUs))
    {
        return Invalid;
    }

    const float distanceCm{static_cast<float>(echoWidthUs) / UsPerCm};

    return ((distanceCm >= MinDistanceCm) && (distanceCm <= MaxDistanceCm)) ? distanceCm : Invalid;
}

// -----------------------------------------------------------------------------
bool SRF05::isInitialized() const noexcept
{
    return myInitialized;
}

// -----------------------------------------------------------------------------
void SRF05::trigger(const std::int64_t nowUs) noexcept
{
    portENTER_CRITICAL(&myMux);
    myTriggerUs    = nowUs;
    myAwaitingEcho = true;
    myEchoHigh     = false;
    portEXIT_CRITICAL(&myMux);

    myTrigger.write(true);
    esp_rom_delay_us(TriggerPulseUs);
    myTrigger.write(false);
}

// -----------------------------------------------------------------------------
void SRF05::onEcho(void* arg) noexcept
{
    auto* self = static_cast<SRF05*>(arg);
    const std::int64_t nowUs{esp_timer_get_time()};
    const bool echoHigh{self->myEcho.read()};

    portENTER_CRITICAL_ISR(&self->myMux);

    if (self->myAwaitingEcho)
    {
        if (echoHigh)
        {
            // Rising edge: the burst has been sent and the sensor is listening.
            self->myEchoStartUs = nowUs;
            self->myEchoHigh    = true;
        }
        else if (self->myEchoHigh)
        {
            // Falling edge: the echo has returned (or the sensor timed out).
            self->myEchoWidthUs  = nowUs - self->myEchoStartUs;
            self->myResultUs     = nowUs;
            self->myEchoHigh     = false;
            self->myAwaitingEcho = false;
        }
    }

    portEXIT_CRITICAL_ISR(&self->myMux);
}

} // namespace driver::distance_sensor
