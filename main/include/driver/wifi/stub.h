/**
 * @file stub.h
 * @brief WiFi driver stub for simulation.
 */
#pragma once

#include "driver/wifi/interface.h"

namespace driver::wifi
{
/**
 * @brief Simulated WiFi driver.
 */
class Stub final : public Interface
{
public:
    /**
     * @brief Constructor.
     */
    Stub() noexcept = default;

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Simulate connecting to WiFi.
     * @return Always true.
     */
    bool connect() noexcept override
    {
        myInitialized = true;
        myConnected   = true;
        return true;
    }

    /**
     * @brief Simulate a non-blocking WiFi reconnect request.
     * @return Always true.
     */
    bool reconnect() noexcept override
    {
        myInitialized = true;
        myConnected   = true;
        return true;
    }

    /**
     * @brief Simulate disconnecting from WiFi.
     */
    void disconnect() noexcept override
    {
        myConnected = false;
    }

    /**
     * @brief Check simulated WiFi connection state.
     * @return True if simulated WiFi is connected.
     */
    bool isConnected() const noexcept override
    {
        return myConnected;
    }

    /**
     * @brief Check simulated WiFi initialization state.
     * @return True if connect has been called.
     */
    bool isInitialized() const noexcept override
    {
        return myInitialized;
    }

    Stub(const Stub&)            = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(const Stub&) = delete;
    Stub& operator=(Stub&&)      = delete;

private:
    /** @brief Simulated initialization state. */
    bool myInitialized{false};

    /** @brief Simulated connection state. */
    bool myConnected{false};
};
} // namespace driver::wifi
