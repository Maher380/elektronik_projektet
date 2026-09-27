#include <cstddef>
#include <cstdint>
#include <cstring>

#include "driver/wifi/esp32s3.h"

extern "C" {
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs_flash.h"
}

namespace
{
void copyWifiString(std::uint8_t* dst, std::size_t dstSize, const char* src) noexcept
{
    if ((dst == nullptr) || (dstSize == 0U)) { return; }

    std::size_t count{0U};
    if (src != nullptr)
    {
        while ((src[count] != '\0') && (count < (dstSize - 1U)))
        {
            dst[count] = static_cast<std::uint8_t>(src[count]);
            ++count;
        }
    }

    dst[count] = 0U;
}

bool isOkOrAlreadyDone(esp_err_t result) noexcept
{
    return (result == ESP_OK) || (result == ESP_ERR_INVALID_STATE);
}
} // namespace

namespace driver::wifi
{
// -----------------------------------------------------------------------------
Esp32s3::Esp32s3(const char* ssid, const char* password) noexcept
    : mySsid{ssid}
    , myPassword{password}
    , myNetif{nullptr}
    , myWifiEventHandler{nullptr}
    , myIpEventHandler{nullptr}
    , myInitialized{false}
    , myConnected{false}
{}

// -----------------------------------------------------------------------------
Esp32s3::~Esp32s3() noexcept
{
    disconnect();
}

// -----------------------------------------------------------------------------
bool Esp32s3::connect() noexcept
{
    if (mySsid == nullptr) { return false; }
    if (myConnected.load()) { return true; }

    if (myInitialized.load())
    {
        return esp_wifi_connect() == ESP_OK;
    }

    esp_err_t result = nvs_flash_init();
    if ((result == ESP_ERR_NVS_NO_FREE_PAGES) || (result == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        if (nvs_flash_erase() != ESP_OK) { return false; }
        result = nvs_flash_init();
    }
    if (result != ESP_OK) { return false; }

    if (!isOkOrAlreadyDone(esp_netif_init())) { return false; }
    if (!isOkOrAlreadyDone(esp_event_loop_create_default())) { return false; }

    myNetif = esp_netif_create_default_wifi_sta();
    if (myNetif == nullptr) { return false; }

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&config) != ESP_OK)
    {
        esp_netif_destroy_default_wifi(myNetif);
        myNetif = nullptr;
        return false;
    }

    myInitialized.store(true);

    // Credentials already come from firmware configuration; do not persist them again.
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK)
    {
        disconnect();
        return false;
    }

    if (esp_event_handler_instance_register(WIFI_EVENT,
                                            ESP_EVENT_ANY_ID,
                                            &Esp32s3::eventHandler,
                                            this,
                                            &myWifiEventHandler)
        != ESP_OK)
    {
        disconnect();
        return false;
    }

    if (esp_event_handler_instance_register(IP_EVENT,
                                            IP_EVENT_STA_GOT_IP,
                                            &Esp32s3::eventHandler,
                                            this,
                                            &myIpEventHandler)
        != ESP_OK)
    {
        disconnect();
        return false;
    }

    wifi_config_t wifiConfig{};
    copyWifiString(wifiConfig.sta.ssid, sizeof(wifiConfig.sta.ssid), mySsid);
    copyWifiString(wifiConfig.sta.password, sizeof(wifiConfig.sta.password), myPassword);

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
    {
        disconnect();
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &wifiConfig) != ESP_OK)
    {
        disconnect();
        return false;
    }

    if (esp_wifi_start() != ESP_OK)
    {
        disconnect();
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
bool Esp32s3::reconnect() noexcept
{
    if (myConnected.load()) { return true; }
    if (!myInitialized.load()) { return false; }
    return esp_wifi_connect() == ESP_OK;
}

// -----------------------------------------------------------------------------
void Esp32s3::disconnect() noexcept
{
    if (!myInitialized.load()) { return; }

    esp_wifi_disconnect();
    esp_wifi_stop();

    if (myWifiEventHandler != nullptr)
    {
        esp_event_handler_instance_unregister(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              myWifiEventHandler);
        myWifiEventHandler = nullptr;
    }

    if (myIpEventHandler != nullptr)
    {
        esp_event_handler_instance_unregister(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              myIpEventHandler);
        myIpEventHandler = nullptr;
    }

    esp_wifi_deinit();

    if (myNetif != nullptr)
    {
        esp_netif_destroy_default_wifi(myNetif);
        myNetif = nullptr;
    }

    myConnected.store(false);
    myInitialized.store(false);
}

// -----------------------------------------------------------------------------
bool Esp32s3::isConnected() const noexcept
{
    return myConnected.load();
}

// -----------------------------------------------------------------------------
bool Esp32s3::isInitialized() const noexcept
{
    return myInitialized.load();
}

// -----------------------------------------------------------------------------
void Esp32s3::eventHandler(void* arg,
                           esp_event_base_t eventBase,
                           int32_t eventId,
                           void* eventData)
{
    (void)eventData;

    auto* self = static_cast<Esp32s3*>(arg);
    if (self == nullptr) { return; }

    if ((eventBase == WIFI_EVENT) && (eventId == WIFI_EVENT_STA_START))
    {
        esp_wifi_connect();
    }
    else if ((eventBase == WIFI_EVENT) && (eventId == WIFI_EVENT_STA_DISCONNECTED))
    {
        self->myConnected.store(false);
    }
    else if ((eventBase == IP_EVENT) && (eventId == IP_EVENT_STA_GOT_IP))
    {
        self->myConnected.store(true);
    }
}
} // namespace driver::wifi
