#include <cstdint>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"

#include "driver/odometer/a3144.h"
#include "system/pin_manager/esp32s3.h"

namespace driver::odometer
{
namespace
{
/** Pulses closer together than this are treated as noise and ignored (1 ms -> max 1 kHz). */
constexpr std::int64_t MinPulseIntervalUs{1'000};

/** Speed reads as zero if no pulse has been seen for this long (1 s). */
constexpr std::int64_t StandstillTimeoutUs{1'000'000};

constexpr float UsToS{1.0e-6F};

// Singleton pin manager instance.
auto& myPinManager = sys::pin_manager::Esp32s3::instance();

} // namespace

// -----------------------------------------------------------------------------
A3144::A3144(const Config& config) noexcept
    : myConfig{config}
    , myDistancePerPulse{distancePerPulse(config)}
    , myPulseCount{0U}
    , myLastPulseUs{0}
    , myPulsePeriodUs{0}
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

    // Try to book the GPIO pin via the pin manager, return false on failure.
    if (!myPinManager.reservePin(myConfig.pin)) { return false; }

    reset();

    // A3144 output is open-collector and active-low: use the internal pull-up
    // and count the falling edge that occurs when a magnet passes the sensor.
    gpio_config_t config{};
    config.pin_bit_mask = (1ULL << myConfig.pin);
    config.mode         = GPIO_MODE_INPUT;
    config.pull_up_en   = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type    = GPIO_INTR_NEGEDGE;

    if (gpio_config(&config) != ESP_OK)
    {
        myPinManager.releasePin(myConfig.pin);
        return false;
    }

    // The ISR service is shared by all pins. ESP_ERR_INVALID_STATE means it is already installed.
    const esp_err_t serviceResult{gpio_install_isr_service(0)};
    if ((serviceResult != ESP_OK) && (serviceResult != ESP_ERR_INVALID_STATE))
    {
        gpio_reset_pin(static_cast<gpio_num_t>(myConfig.pin));
        myPinManager.releasePin(myConfig.pin);
        return false;
    }

    if (gpio_isr_handler_add(static_cast<gpio_num_t>(myConfig.pin), onPulse, this) != ESP_OK)
    {
        gpio_reset_pin(static_cast<gpio_num_t>(myConfig.pin));
        myPinManager.releasePin(myConfig.pin);
        return false;
    }

    myInitialized = true;
    return true;
}

// -----------------------------------------------------------------------------
bool A3144::deinit() noexcept
{
    // Return false if init() never succeeded.
    if (!myInitialized) { return false; }

    gpio_isr_handler_remove(static_cast<gpio_num_t>(myConfig.pin));
    gpio_reset_pin(static_cast<gpio_num_t>(myConfig.pin));
    myPinManager.releasePin(myConfig.pin);

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
    portEXIT_CRITICAL(&myMux);

    // Need at least two pulses to know the period, and a recent one to be moving.
    if ((periodUs <= 0) || ((esp_timer_get_time() - lastPulseUs) > StandstillTimeoutUs))
    {
        return 0.0F;
    }

    return myDistancePerPulse / (static_cast<float>(periodUs) * UsToS);
}

// -----------------------------------------------------------------------------
void A3144::reset() noexcept
{
    portENTER_CRITICAL(&myMux);
    myPulseCount    = 0U;
    myLastPulseUs   = 0;
    myPulsePeriodUs = 0;
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
        self->myPulsePeriodUs = hasPrevious ? sinceLastUs : 0;
        self->myLastPulseUs   = nowUs;
        ++self->myPulseCount;
    }

    portEXIT_CRITICAL_ISR(&self->myMux);
}

} // namespace driver::odometer
