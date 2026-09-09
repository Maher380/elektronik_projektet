/**
 * @file esp32s3.h
 * @brief WiFi driver for ESP32-S3.
 */
#pragma once

#include "driver/wifi/interface.h"

#include <atomic>
#include <cstdint>

extern "C" {
#include "esp_event.h"
#include "esp_netif.h"
}

namespace driver::wifi
{
/**
 * @brief WiFi station driver implementation for ESP32-S3.
 */
class Esp32s3 final : public Interface
{
public:
    /**
     * @brief Constructor.
     * @param[in] ssid WiFi network SSID.
     * @param[in] password WiFi network password.
     */
    Esp32s3(const char* ssid, const char* password) noexcept;

    /**
     * @brief Destructor.
     */
    ~Esp32s3() noexcept override;

    /**
     * @brief Initialize WiFi and start connecting asynchronously.
     * @return True if the connection attempt was started.
     */
    bool connect() noexcept override;

    /**
     * @brief Request a reconnect without waiting for the result.
     * @return True if a reconnect attempt was started or WiFi is already connected.
     */
    bool reconnect() noexcept override;

    /**
     * @brief Disconnect from the configured WiFi network.
     */
    void disconnect() noexcept override;

    /**
     * @brief Check if WiFi is connected.
     * @return True if connected and ready for network traffic.
     */
    bool isConnected() const noexcept override;

    /**
     * @brief Check if the WiFi driver is initialized.
     * @return True if initialized.
     */
    bool isInitialized() const noexcept override;

    Esp32s3(const Esp32s3&)            = delete;
    Esp32s3(Esp32s3&&)                 = delete;
    Esp32s3& operator=(const Esp32s3&) = delete;
    Esp32s3& operator=(Esp32s3&&)      = delete;

private:
    /**
     * @brief ESP-IDF WiFi and IP event callback.
     */
    static void eventHandler(void* arg,
                             esp_event_base_t eventBase,
                             int32_t eventId,
                             void* eventData);

    /** @brief SSID used by this driver instance. */
    const char* mySsid;

    /** @brief Password used by this driver instance. */
    const char* myPassword;

    /** @brief Default WiFi station network interface. */
    esp_netif_t* myNetif;

    /** @brief Registered WiFi event handler instance. */
    esp_event_handler_instance_t myWifiEventHandler;

    /** @brief Registered IP event handler instance. */
    esp_event_handler_instance_t myIpEventHandler;

    /** @brief True after ESP-IDF WiFi has been initialized. */
    std::atomic<bool> myInitialized;

    /** @brief True when the station is connected and has an IP address. */
    std::atomic<bool> myConnected;
};
} // namespace driver::wifi
