#include <cstddef>
#include <cstdint>
#include <memory>

#include "driver/nvs/esp32s3.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace driver::nvs
{
namespace
{
constexpr const char* LogTag{"nvs"};

class Esp32s3Handle final : public Handle
{
public:
    Esp32s3Handle(const nvs_handle_t handle, bool& open) noexcept
        : myHandle{handle}
        , myOpen{open}
    {}

    ~Esp32s3Handle() noexcept override
    {
        nvs_close(myHandle);
        myOpen = false;
    }

    bool setU8(const char* key, const std::uint8_t value) noexcept override
    {
        return saved(nvs_set_u8(myHandle, key, value));
    }

    bool getU8(const char* key, std::uint8_t& value) noexcept override
    {
        return nvs_get_u8(myHandle, key, &value) == ESP_OK;
    }

    bool setU32(const char* key, const std::uint32_t value) noexcept override
    {
        return saved(nvs_set_u32(myHandle, key, value));
    }

    bool getU32(const char* key, std::uint32_t& value) noexcept override
    {
        return nvs_get_u32(myHandle, key, &value) == ESP_OK;
    }

    bool setString(const char* key, const char* value) noexcept override
    {
        return (value != nullptr) && saved(nvs_set_str(myHandle, key, value));
    }

    bool getString(const char* key, char* value, const std::size_t size) noexcept override
    {
        std::size_t length{size};
        return (value != nullptr) && (nvs_get_str(myHandle, key, value, &length) == ESP_OK);
    }

    bool eraseKey(const char* key) noexcept override { return saved(nvs_erase_key(myHandle, key)); }

    bool eraseAll() noexcept override { return saved(nvs_erase_all(myHandle)); }

private:
    /** Save a change to flash right away. */
    bool saved(const esp_err_t result) noexcept
    {
        return (result == ESP_OK) && (nvs_commit(myHandle) == ESP_OK);
    }

    const nvs_handle_t myHandle;
    bool& myOpen;
};

} // namespace

bool Esp32s3::init() noexcept
{
    if (myInitialized) { return true; }

    esp_err_t result{nvs_flash_init()};

    // The partition cannot be used as it is, erasing it is the only way on.
    if ((result == ESP_ERR_NVS_NO_FREE_PAGES) || (result == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        ESP_LOGW(LogTag, "NVS unusable (%s), erasing: all stored settings are lost", esp_err_to_name(result));
        if (nvs_flash_erase() != ESP_OK) { return false; }
        result = nvs_flash_init();
    }

    myInitialized = (result == ESP_OK);
    return myInitialized;
}

bool Esp32s3::isInitialized() const noexcept
{
    return myInitialized;
}

std::unique_ptr<Handle> Esp32s3::open(const Namespace ns) noexcept
{
    const auto index{static_cast<std::size_t>(ns)};
    if (!myInitialized || (index >= static_cast<std::size_t>(Namespace::Count)) || myOpen[index])
    {
        return nullptr;
    }

    nvs_handle_t handle{0U};
    if (nvs_open(NamespaceNames[index], NVS_READWRITE, &handle) != ESP_OK) { return nullptr; }

    myOpen[index] = true;
    return std::make_unique<Esp32s3Handle>(handle, myOpen[index]);
}

} // namespace driver::nvs
