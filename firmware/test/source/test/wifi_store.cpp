#include <cstdio>
#include <cstring>

#include "driver/nvs/stub.h"
#include "driver/wifi/store.h"
#include "test/wifi_store.h"

namespace
{
bool expect(bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("Wi-Fi store test failed: %s\n", message);
        return false;
    }

    return true;
}
} // namespace

namespace test
{
bool runWifiStoreTest() noexcept
{
    using driver::wifi::Store;

    driver::nvs::Stub nvs;
    if (!expect(nvs.init(), "NVS init")) { return false; }

    char ssid[driver::wifi::SsidMaxLength + 1U]{"untouched"};
    char password[driver::wifi::PasswordMaxLength + 1U]{"untouched"};

    {
        Store store{nvs};
        if (!expect(store.isOpen(), "store should open")) { return false; }
        if (!expect(!Store{nvs}.isOpen(), "a second owner must not open the namespace")) { return false; }
        if (!expect(!store.load(ssid, password), "nothing stored should load as false")) { return false; }
        if (!expect(std::strcmp(ssid, "untouched") == 0, "a failed load must leave the buffers alone")) { return false; }

        if (!expect(!store.save("", "password1"), "empty SSID must be refused")) { return false; }
        if (!expect(!store.save("123456789012345678901234567890123", "password1"), "33-byte SSID must be refused"))
        { return false; }
        if (!expect(!store.save("net", "short"), "a password under 8 characters must be refused")) { return false; }
        if (!expect(!store.load(ssid, password), "refused saves must store nothing")) { return false; }

        if (!expect(store.save("cnb net", "cnbrules"), "valid network should save")) { return false; }
    }

    // A new owner, as after a restart, reads the same network back.
    {
        const Store store{nvs};
        if (!expect(store.load(ssid, password), "stored network should load")) { return false; }
        if (!expect((std::strcmp(ssid, "cnb net") == 0) && (std::strcmp(password, "cnbrules") == 0),
                    "SSID and password should round trip, spaces included")) { return false; }
    }

    {
        Store store{nvs};
        if (!expect(store.save("open", ""), "an open network has no password")) { return false; }
        if (!expect(store.load(ssid, password) && (std::strcmp(ssid, "open") == 0) && (password[0] == '\0'),
                    "open network should round trip")) { return false; }

        char longest[driver::wifi::PasswordMaxLength + 1U]{};
        std::memset(longest, 'x', driver::wifi::PasswordMaxLength);
        if (!expect(store.save("long", longest) && store.load(ssid, password)
                        && (std::strlen(password) == driver::wifi::PasswordMaxLength),
                    "a 63-character password should round trip")) { return false; }

        if (!expect(store.clear() && !store.load(ssid, password), "clear should forget the network")) { return false; }
    }

    return true;
}
} // namespace test
