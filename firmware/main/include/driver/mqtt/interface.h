/**
 * @file interface.h
 * @brief Abstract MQTT driver interface.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace driver::mqtt
{

inline constexpr std::size_t TopicSize{96U};
// Includes full-precision telemetry, raw ADC counts, runtime state and driver style.
inline constexpr std::size_t PayloadSize{768U};

/** MQTT quality-of-service levels used by this project. */
enum class Qos : std::uint8_t
{
    AtMostOnce = 0U,
    AtLeastOnce = 1U,
};

/** Immutable MQTT client configuration. All strings must outlive the driver. */
struct Config
{
    const char* brokerUri{nullptr};
    const char* clientId{nullptr};
    const char* username{nullptr};
    const char* password{nullptr};
    std::uint16_t keepAliveSeconds{30U};
    const char* lastWillTopic{nullptr};
    const char* lastWillPayload{nullptr};
    Qos lastWillQos{Qos::AtLeastOnce};
    bool lastWillRetain{true};
};

/** Complete MQTT message copied out of the ESP-MQTT event task. */
struct Message
{
    std::array<char, TopicSize> topic{};
    std::array<char, PayloadSize> payload{};
    Qos qos{Qos::AtMostOnce};
    bool retained{false};
};

/** Abstract interface for an asynchronous MQTT client. */
class Interface
{
public:
    virtual ~Interface() noexcept = default;

    /** Start the client asynchronously. */
    virtual bool connect() noexcept = 0;

    /** Request asynchronous reconnection of an initialized client. */
    virtual bool reconnect() noexcept = 0;

    /** Stop and destroy the client. */
    virtual void disconnect() noexcept = 0;

    /** Return true after the client has been initialized and started. */
    virtual bool isInitialized() const noexcept = 0;

    /** Return true while connected to the broker. */
    virtual bool isConnected() const noexcept = 0;

    /** Queue a message for the MQTT task without performing network I/O here. */
    virtual bool publish(const char* topic,
                         const char* payload,
                         Qos qos,
                         bool retained) noexcept = 0;

    /**
     * @brief Register a subscription that is restored after every connection.
     *
     * Subscriptions must be registered before connect() is called.
     */
    virtual bool subscribe(const char* topic, Qos qos) noexcept = 0;

    /** Copy the oldest complete received message, if one is available. */
    virtual bool readMessage(Message& message) noexcept = 0;

    /** Return and clear the fail-safe receive-overflow indication. */
    virtual bool consumeReceiveOverflow() noexcept = 0;

    /** Return and clear a connection loss, including a loss between control ticks. */
    virtual bool consumeDisconnect() noexcept = 0;

    Interface(const Interface&) = delete;
    Interface& operator=(const Interface&) = delete;
    Interface(Interface&&) = delete;
    Interface& operator=(Interface&&) = delete;

protected:
    Interface() noexcept = default;
};

} // namespace driver::mqtt
