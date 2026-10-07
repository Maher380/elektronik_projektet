#include "driver/start_module/gpio.h"

namespace driver::start_module
{
Gpio::Gpio(driver::gpio::Interface& input, const std::uint32_t holdTimeMs) noexcept
    : myInput{input}
    , myHoldTimeMs{holdTimeMs}
    , myHighSinceMs{0U}
    , mySeenLow{false}
    , myHigh{false}
    , myStarted{false}
{}

void Gpio::update(const std::uint32_t nowMs) noexcept
{
    if (!isInitialized() || myStarted) { return; }

    if (!myInput.read())
    {
        mySeenLow = true;
        myHigh    = false;
        return;
    }

    // High without a low first: the module was not reset, so wait for a new start.
    if (!mySeenLow) { return; }

    if (!myHigh)
    {
        myHigh        = true;
        myHighSinceMs = nowMs;
    }

    // Unsigned subtraction keeps the elapsed time right when nowMs wraps around.
    if ((nowMs - myHighSinceMs) >= myHoldTimeMs) { myStarted = true; }
}

bool Gpio::isStarted() const noexcept { return myStarted; }

bool Gpio::isInitialized() const noexcept { return myInput.isInitialized(); }
} // namespace driver::start_module
