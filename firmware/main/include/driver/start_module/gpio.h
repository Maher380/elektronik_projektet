#pragma once

#include <cstdint>
#include "driver/gpio/interface.h"

namespace driver::start_module
{
enum class State : std::uint8_t { Waiting, Started, Stopped };

// Adapted from PR #36's GPIO start module, without its unrelated changes.
class Gpio final
{
public:
    explicit Gpio(driver::gpio::Interface& input, std::uint32_t holdMs = 20U) noexcept
        : myInput{input}, myHoldMs{holdMs} {}

    void update(std::uint32_t nowMs) noexcept
    {
        if (!myInput.isInitialized()) { myState = State::Stopped; return; }
        const bool high = myInput.read();
        if (myState == State::Started)
        {
            if (!high) { myState = State::Stopped; }
            return;
        }
        if (myState == State::Stopped) { return; }
        if (!high) { mySeenLow = true; myHigh = false; return; }
        if (!mySeenLow) { return; }
        if (!myHigh) { myHigh = true; myHighSinceMs = nowMs; }
        if (nowMs - myHighSinceMs >= myHoldMs) { myState = State::Started; }
    }

    State state() const noexcept { return myState; }

private:
    driver::gpio::Interface& myInput;
    const std::uint32_t myHoldMs;
    std::uint32_t myHighSinceMs{0U};
    bool mySeenLow{false};
    bool myHigh{false};
    State myState{State::Waiting};
};
}
