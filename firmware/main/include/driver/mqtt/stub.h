/**
 * @file stub.h
 * @brief MQTT driver stub for host tests.
 */

#pragma once

#include <array>
#include <cstring>

#include "driver/mqtt/interface.h"

namespace driver::mqtt
{

class Stub final : public Interface
{
public:
    explicit Stub(const Config& config = {}) noexcept
        : myConfig{config}
    {}

    ~Stub() noexcept override = default;

    bool connect() noexcept override
    {
        myInitialized = true;
        myConnected = true;
        return true;
    }

    bool reconnect() noexcept override
    {
        if (!myInitialized) { return false; }
        myConnected = true;
        return true;
    }

    void disconnect() noexcept override
    {
        myDisconnected = myDisconnected || myConnected;
        myConnected = false;
        myInitialized = false;
    }

    bool isInitialized() const noexcept override { return myInitialized; }
    bool isConnected() const noexcept override { return myConnected; }

    bool publish(const char* topic,
                 const char* payload,
                 Qos qos,
                 bool retained) noexcept override
    {
        if (!myConnected || !copyField(myLastPublished.topic, topic)
            || !copyField(myLastPublished.payload, payload))
        {
            return false;
        }

        myLastPublished.qos = qos;
        myLastPublished.retained = retained;
        myHasPublishedMessage = true;
        return true;
    }

    bool subscribe(const char* topic, Qos qos) noexcept override
    {
        if (myInitialized || (mySubscriptionCount >= mySubscriptions.size()))
        {
            return false;
        }
        auto& subscription = mySubscriptions[mySubscriptionCount];
        if (!copyField(subscription.topic, topic)) { return false; }
        subscription.qos = qos;
        ++mySubscriptionCount;
        return true;
    }

    bool readMessage(Message& message) noexcept override
    {
        if (myReceiveCount == 0U) { return false; }
        message = myIncoming[myReceiveHead];
        myReceiveHead = (myReceiveHead + 1U) % myIncoming.size();
        --myReceiveCount;
        return true;
    }

    bool consumeReceiveOverflow() noexcept override
    {
        const bool overflowed = myReceiveOverflow;
        myReceiveOverflow = false;
        return overflowed;
    }

    /** Queue an incoming message for a host test. */
    bool simulateIncoming(const char* topic,
                          const char* payload,
                          Qos qos = Qos::AtLeastOnce,
                          bool retained = false) noexcept
    {
        if (myReceiveCount >= myIncoming.size())
        {
            myReceiveOverflow = true;
            return false;
        }

        auto& message = myIncoming[myReceiveTail];
        if (!copyField(message.topic, topic) || !copyField(message.payload, payload))
        {
            myReceiveOverflow = true;
            return false;
        }
        message.qos = qos;
        message.retained = retained;
        myReceiveTail = (myReceiveTail + 1U) % myIncoming.size();
        ++myReceiveCount;
        return true;
    }

    bool consumeDisconnect() noexcept override
    {
        const bool lost = myDisconnected;
        myDisconnected = false;
        return lost;
    }

    bool hasPublishedMessage() const noexcept { return myHasPublishedMessage; }
    const Message& lastPublishedMessage() const noexcept { return myLastPublished; }

    Stub(const Stub&) = delete;
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&) = delete;
    Stub& operator=(Stub&&) = delete;

private:
    struct Subscription
    {
        std::array<char, TopicSize> topic{};
        Qos qos{Qos::AtMostOnce};
    };

    template<std::size_t Size>
    static bool copyField(std::array<char, Size>& destination, const char* source) noexcept
    {
        if (source == nullptr) { return false; }
        const std::size_t length = std::strlen(source);
        if (length >= Size) { return false; }
        std::memcpy(destination.data(), source, length + 1U);
        return true;
    }

    Config myConfig;
    bool myInitialized{false};
    bool myConnected{false};
    bool myDisconnected{false};
    std::array<Subscription, 4U> mySubscriptions{};
    std::size_t mySubscriptionCount{0U};
    std::array<Message, 8U> myIncoming{};
    std::size_t myReceiveHead{0U};
    std::size_t myReceiveTail{0U};
    std::size_t myReceiveCount{0U};
    bool myReceiveOverflow{false};
    Message myLastPublished{};
    bool myHasPublishedMessage{false};
};

} // namespace driver::mqtt
