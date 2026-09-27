#include <cstdint>

#include "driver/gpio.h"

#include "driver/gpio/direction.h"
#include "driver/gpio/edge.h"
#include "driver/gpio/esp32s3.h"
#include "system/pin_manager/esp32s3.h"
#include "esp_log.h"

namespace driver::gpio
{
// -----------------------------------------------------------------------------
namespace
{
gpio_mode_t gpioModeFromDirection(const Direction direction) noexcept
{
    switch (direction)
    {
    case Direction::Input:
    case Direction::InputPullup:
        return GPIO_MODE_INPUT;
    case Direction::Output:
        return GPIO_MODE_OUTPUT;
    }

    return GPIO_MODE_DISABLE;
}

gpio_int_type_t gpioIntrTypeFromEdge(const Edge edge) noexcept
{
    switch (edge)
    {
    case Edge::Rising:
        return GPIO_INTR_POSEDGE;
    case Edge::Falling:
        return GPIO_INTR_NEGEDGE;
    case Edge::Both:
        return GPIO_INTR_ANYEDGE;
    }

    return GPIO_INTR_DISABLE;
}
} // namespace

// -----------------------------------------------------------------------------

/** Singleton pin manager instance. */
auto& myPinManager = sys::pin_manager::Esp32s3::instance();

// -----------------------------------------------------------------------------
Esp32s3::Esp32s3(std::uint8_t pin, Direction direction) noexcept
    : myPin{pin}
    , myDirection{direction}
    , myInitialized{false}
    , myState{false}
    , myInterruptEnabled{false}
{
    // Validate and reserve pin.
    if (!myPinManager.reservePin(myPin)) { return; }

    // Create GPIO config.
    // Configure GPIO mode based on direction.
    gpio_config_t config{};
    config.pin_bit_mask = (1ULL << pin);
    config.mode = gpioModeFromDirection(direction);

    // Disable pull-down resistor and interrupts.
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type    = GPIO_INTR_DISABLE;
    
    // Enable pull-up resistor if specified.
    const auto pullup = direction == Direction::InputPullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    config.pull_up_en = pullup;
    const esp_err_t result = gpio_config(&config);

    // Store initialization state.
    myInitialized = (result == ESP_OK);

    // Release pin if initialization failed.
    if (!myInitialized)
    {
        myPinManager.releasePin(myPin);
    }
}

// -----------------------------------------------------------------------------
Esp32s3::~Esp32s3() noexcept 
{
    // Only clean up a pin this instance actually reserved and configured.
    if (!myInitialized) { return; }

    // Unregister the interrupt handler, if any.
    disableInterrupt();

    // Reset the gpio pin and pin manager.
    gpio_reset_pin(static_cast<gpio_num_t>(myPin));
    myPinManager.releasePin(myPin);
}

// -----------------------------------------------------------------------------
void Esp32s3::write(bool state) noexcept
{
    // Ignore if the pin is not owned by this instance.
    if (!myInitialized) { return; }

    // Check data direction, ignore if input.
    if (Direction::Output != myDirection) { return; }

    if (ESP_OK == gpio_set_level(static_cast<gpio_num_t>(myPin), state))
    {
        myState = state;
    }
}

// -----------------------------------------------------------------------------
bool Esp32s3::read() const noexcept
{
    // Read as low if the pin is not owned by this instance.
    if (!myInitialized) { return false; }

    // Read state, cast to bool (1 => true, 0 => false).
    const uint8_t gpioLevel = gpio_get_level(static_cast<gpio_num_t>(myPin));
    const bool state = static_cast<bool>(gpioLevel);

    return state; 
}

// -----------------------------------------------------------------------------
void Esp32s3::toggle() noexcept
{
    // Toggle the last commanded output state.
    write(!myState);
}

// -----------------------------------------------------------------------------
bool Esp32s3::isInitialized() const noexcept
{
    return myInitialized;
}

// -----------------------------------------------------------------------------
bool Esp32s3::enableInterrupt(const Edge edge, const InterruptCallback callback, void* arg) noexcept
{
    // Only one handler per pin, and only on a configured pin.
    if (!myInitialized || myInterruptEnabled || (nullptr == callback)) { return false; }

    const auto pin = static_cast<gpio_num_t>(myPin);

    if (ESP_OK != gpio_set_intr_type(pin, gpioIntrTypeFromEdge(edge))) { return false; }

    // The ISR service is shared by all pins. ESP_ERR_INVALID_STATE means it is already installed.
    const esp_err_t serviceResult{gpio_install_isr_service(0)};
    if ((ESP_OK != serviceResult) && (ESP_ERR_INVALID_STATE != serviceResult))
    {
        gpio_set_intr_type(pin, GPIO_INTR_DISABLE);
        return false;
    }

    if (ESP_OK != gpio_isr_handler_add(pin, callback, arg))
    {
        gpio_set_intr_type(pin, GPIO_INTR_DISABLE);
        return false;
    }

    if (ESP_OK != gpio_intr_enable(pin))
    {
        gpio_isr_handler_remove(pin);
        gpio_set_intr_type(pin, GPIO_INTR_DISABLE);
        return false;
    }

    myInterruptEnabled = true;
    return true;
}

// -----------------------------------------------------------------------------
void Esp32s3::disableInterrupt() noexcept
{
    if (!myInterruptEnabled) { return; }

    const auto pin = static_cast<gpio_num_t>(myPin);
    gpio_intr_disable(pin);
    gpio_isr_handler_remove(pin);
    gpio_set_intr_type(pin, GPIO_INTR_DISABLE);

    myInterruptEnabled = false;
}
} // namespace driver::gpio
