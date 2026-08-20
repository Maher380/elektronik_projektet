#include <cstdint>

#include "system/pin_manager/esp32s3.h"

namespace sys::pin_manager
{
namespace
{
constexpr std::uint8_t PinMax{47U};

} // namespace

Interface& Esp32s3::instance() noexcept
{
    // Create singleton pin manager (initialized once during startup).
    static Esp32s3 myInstance{};

    // Return reference to the instance, cast to the interface type.
    return myInstance;

}

bool Esp32s3::isPinValid(const std::uint8_t pin) const noexcept
{
    // Return false if the pin number is out of range.
    if (PinMax < pin) { return false; }

    // Pins to avoid (boot strapping pins).
    constexpr std::uint8_t invalidPins[]{0U, 2U, 12U, 15U};

    // Check through invalid pins list, return false on match.
    for (const auto invalidPin : invalidPins)
    {
        if (invalidPin == pin) { return false; }
    }
    return true;
}

bool Esp32s3::isPinBusy(std::uint8_t pin) const noexcept
{
    if ((pin > PinMax) || !isPinValid(pin)) { return false; }
    return (myPinReg & (1ULL << pin)) != 0;
};

bool Esp32s3::reservePin(std::uint8_t pin) noexcept
{
    if (!isPinValid(pin) || isPinBusy(pin)) { return false; }

    myPinReg |= (1ULL << pin);
    return true;
};

void Esp32s3::releasePin(std::uint8_t pin) noexcept
{   
    if (pin > PinMax) { return; }
    myPinReg &= ~(1ULL << pin);
};

Esp32s3::Esp32s3() noexcept
    : myPinReg{}
{}
} // namespace sys::pin_manager
