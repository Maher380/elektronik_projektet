/**
 * @file interface.h
 * @brief WiFi driver interface.
 */
#pragma once

namespace driver::wifi
{
/**
 * @brief WiFi driver interface.
 */
class Interface
{
public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Initialize WiFi and start connecting without waiting for an IP address.
     * @return True if the asynchronous connection attempt was started.
     */
    virtual bool connect() noexcept = 0;

    /**
     * @brief Request a reconnect without blocking the caller.
     * @return True if a reconnect attempt was started or WiFi is already connected.
     */
    virtual bool reconnect() noexcept = 0;

    /**
     * @brief Disconnect from the WiFi network.
     */
    virtual void disconnect() noexcept = 0;

    /**
     * @brief Check if WiFi is connected.
     * @return True if connected to WiFi and ready for network traffic.
     */
    virtual bool isConnected() const noexcept = 0;

    /**
     * @brief Check if the WiFi driver has been initialized.
     * @return True if initialized, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;
};
} // namespace driver::wifi
