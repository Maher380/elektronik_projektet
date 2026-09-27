/**
 * @file esp32s3.h
 * @brief Thread-safe ESP-MQTT driver for ESP32-S3.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "driver/mqtt/interface.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
}

namespace driver::mqtt
{

class Esp32s3 final : public Interface
{
public:
    explicit Esp32s3(const Config& config) noexcept;
    ~Esp32s3() noexcept override;

    bool connect() noexcept override;
    bool reconnect() noexcept override;
    void disconnect() noexcept override;
    bool isInitialized() const noexcept override;
    bool isConnected() const noexcept override;
    bool publish(const char* topic,
                 const char* payload,
                 Qos qos,
                 bool retained) noexcept override;
    bool subscribe(const char* topic, Qos qos) noexcept override;
    bool readMessage(Message& message) noexcept override;
    bool consumeReceiveOverflow() noexcept override;
    bool consumeDisconnect() noexcept override;

    Esp32s3(const Esp32s3&) = delete;
    Esp32s3& operator=(const Esp32s3&) = delete;
    Esp32s3(Esp32s3&&) = delete;
    Esp32s3& operator=(Esp32s3&&) = delete;

private:
    struct Outgoing
    {
        Message message{};
        TickType_t created{0U};
        std::uint32_t generation{0U};
    };

    static void publishWorker(void* argument);
    void runPublisher() noexcept;

    struct Subscription
    {
        std::array<char, TopicSize> topic{};
        Qos qos{Qos::AtMostOnce};
    };

    static constexpr std::size_t SubscriptionCount{4U};
    static constexpr UBaseType_t IncomingQueueLength{8U};

    static void mqttEventHandler(void* handlerArgs,
                                 esp_event_base_t base,
                                 std::int32_t eventId,
                                 void* eventData);
    void handleConnected() noexcept;
    void handleData(const esp_mqtt_event_t& event) noexcept;

    template<std::size_t Size>
    static bool copyField(std::array<char, Size>& destination,
                          const char* source,
                          std::size_t length) noexcept;

    Config myConfig;
    esp_mqtt_client_handle_t myHandle{nullptr};
    QueueHandle_t myIncomingQueue{nullptr};
    QueueHandle_t myTelemetryQueue{nullptr};
    QueueHandle_t myStateQueue{nullptr};
    SemaphoreHandle_t myWorkerDone{nullptr};
    TaskHandle_t myWorker{nullptr};
    std::atomic<bool> myStopping{false};
    std::atomic<bool> myDisconnected{false};
    std::atomic<std::uint32_t> myGeneration{0U};
    std::array<Subscription, SubscriptionCount> mySubscriptions{};
    std::size_t mySubscriptionCount{0U};
    std::atomic<bool> myInitialized{false};
    std::atomic<bool> myConnected{false};
    std::atomic<bool> myReceiveOverflow{false};
};

} // namespace driver::mqtt
