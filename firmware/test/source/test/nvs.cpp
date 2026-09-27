#include <cstdint>
#include <cstdio>
#include <cstring>

#include "driver/nvs/interface.h"
#include "test/nvs.h"

namespace
{
bool expect(bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("NVS test failed: %s\n", message);
        return false;
    }

    return true;
}
} // namespace

namespace test
{
bool runNvsTest(driver::nvs::Interface& nvs) noexcept
{
    using driver::nvs::Namespace;

    if (!expect(nvs.open(Namespace::Test) == nullptr, "open before init should fail")) { return false; }
    if (!expect(nvs.init() && nvs.init(), "init should succeed and be repeatable")) { return false; }

    {
        auto handle{nvs.open(Namespace::Test)};
        if (!expect(handle != nullptr, "open should succeed")) { return false; }
        if (!expect(nvs.open(Namespace::Test) == nullptr, "second open of the same namespace should fail")) { return false; }

        std::uint8_t u8{0U};
        std::uint32_t u32{0U};
        char text[16]{'\0'};

        if (!expect(!handle->getU8("missing", u8), "missing key should read as false")) { return false; }
        if (!expect(handle->setU8("ver", 1U) && handle->getU8("ver", u8) && (u8 == 1U), "u8 round trip")) { return false; }
        if (!expect(handle->setU32("count", 70000U) && handle->getU32("count", u32) && (u32 == 70000U),
                    "u32 round trip")) { return false; }
        if (!expect(handle->setString("car", "FORD") && handle->getString("car", text, sizeof(text)) &&
                    (std::strcmp(text, "FORD") == 0), "string round trip")) { return false; }
        if (!expect(!handle->getString("car", text, 4U), "string must not fit a too small buffer")) { return false; }
        if (!expect(!handle->getU8("count", u8), "reading a key with the wrong type should fail")) { return false; }
        if (!expect(handle->eraseKey("ver") && !handle->getU8("ver", u8), "eraseKey should remove the key")) { return false; }
    }

    // Closing the handle frees the namespace, and the data is still there.
    {
        auto handle{nvs.open(Namespace::Test)};
        std::uint32_t u32{0U};
        if (!expect(handle != nullptr, "reopen after close should succeed")) { return false; }
        if (!expect(handle->getU32("count", u32) && (u32 == 70000U), "data should survive a reopen")) { return false; }
        if (!expect(handle->eraseAll() && !handle->getU32("count", u32), "eraseAll should remove every key")) { return false; }
    }

    std::printf("NVS test succeeded!\n");
    return true;
}
} // namespace test
