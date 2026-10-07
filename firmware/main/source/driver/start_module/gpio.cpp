#include "driver/start_module/gpio.h"

namespace driver::start_module
{
Gpio::Gpio(driver::gpio::Interface& input, const std::uint32_t holdTimeMs) noexcept
    : myInput{input}
    , myHoldTimeMs{holdTimeMs}
    , myHighSinceMs{0U}
    , mySeenLow{false}
    , myHigh{false}
    , myState{State::Waiting}
{}

void Gpio::update(const std::uint32_t nowMs) noexcept
{
    if (!isInitialized()) { return; }

    const bool high{myInput.read()};

    switch (myState)
    {
        case State::Waiting:
            updateWaiting(high, nowMs);
            break;
        case State::Started:
            // The first low read stops the car; no hold time, since a false stop is safe.
            if (!high) { myState = State::Stopped; }
            break;
        case State::Stopped:
            // Final until the car restarts.
            break;
    }
}

State Gpio::state() const noexcept { return myState; }

bool Gpio::isInitialized() const noexcept { return myInput.isInitialized(); }

void Gpio::updateWaiting(const bool high, const std::uint32_t nowMs) noexcept
{
    if (!high)
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
    if ((nowMs - myHighSinceMs) >= myHoldTimeMs) { myState = State::Started; }
}
} // namespace driver::start_module
