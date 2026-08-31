#include <cstdint>
#include <cstdio>

#include "system/pin_manager/interface.h"
#include "test/pin_manager.h"

namespace
{
bool expect(bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("Pin manager test failed: %s\n", message);
        return false;
    }

    return true;
}
} // namespace

namespace test
{
bool runPinManagerTest(sys::pin_manager::Interface& manager) noexcept
{
    constexpr std::uint8_t validPin{1U};
    constexpr std::uint8_t invalidBootPin{0U};
    constexpr std::uint8_t invalidOutOfRangePin{48U};

    manager.releasePin(validPin);

    if (!expect(manager.isPinValid(validPin), "pin 1 should be valid")) { return false; }
    if (!expect(!manager.isPinValid(invalidBootPin), "pin 0 should be invalid")) { return false; }
    if (!expect(!manager.isPinValid(invalidOutOfRangePin), "pin 48 should be invalid")) { return false; }

    if (!expect(!manager.isPinBusy(validPin), "valid pin should not be busy before reservation")) { return false; }
    if (!expect(manager.reservePin(validPin), "valid pin reservation should succeed")) { return false; }
    if (!expect(manager.isPinBusy(validPin), "reserved pin should be busy")) { return false; }
    if (!expect(!manager.reservePin(validPin), "double reservation should fail")) { return false; }

    manager.releasePin(validPin);
    if (!expect(!manager.isPinBusy(validPin), "released pin should not be busy")) { return false; }

    if (!expect(!manager.reservePin(invalidBootPin), "boot pin reservation should fail")) { return false; }
    if (!expect(!manager.reservePin(invalidOutOfRangePin), "out-of-range pin reservation should fail")) { return false; }

    std::printf("Pin manager test succeeded!\n");
    return true;
}
} // namespace test
