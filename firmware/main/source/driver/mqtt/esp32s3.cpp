#include "driver/mqtt/esp32s3.h"

#include <cstring>

extern "C" {
#include "esp_err.h"
}

namespace driver::mqtt
{

Esp32s3::Esp32s3(const Config& config) noexcept
    : myConfig{config}
    , myIncomingQueue{xQueueCreate(IncomingQueueLength, sizeof(Message))}
    , myTelemetryQueue{xQueueCreate(1U, sizeof(Outgoing))}
    , myStateQueue{xQueueCreate(8U, sizeof(Outgoing))}
    , myWorkerDone{xSemaphoreCreateBinary()}
{}

Esp32s3::~Esp32s3() noexcept
{
    disconnect();
    if (myTelemetryQueue != nullptr) { vQueueDelete(myTelemetryQueue); }
    if (myStateQueue != nullptr) { vQueueDelete(myStateQueue); }
    if (myWorkerDone != nullptr) { vSemaphoreDelete(myWorkerDone); }
    if (myIncomingQueue != nullptr)
    {
        vQueueDelete(myIncomingQueue);
        myIncomingQueue = nullptr;
    }
}

bool Esp32s3::connect() noexcept
{
    if (myInitialized.load()) { return true; }
    if ((myIncomingQueue == nullptr) || (myTelemetryQueue == nullptr)
        || (myStateQueue == nullptr) || (myWorkerDone == nullptr)
        || (myConfig.brokerUri == nullptr)
        || (myConfig.clientId == nullptr))
    {
        return false;
    }

    esp_mqtt_client_config_t config{};
    config.broker.address.uri = myConfig.brokerUri;
    config.credentials.client_id = myConfig.clientId;
    config.credentials.username = myConfig.username;
    config.credentials.authentication.password = myConfig.password;
    config.session.disable_clean_session = false;
    config.session.keepalive = static_cast<int>(myConfig.keepAliveSeconds);
    config.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    config.session.last_will.topic = myConfig.lastWillTopic;
    config.session.last_will.msg = myConfig.lastWillPayload;
    config.session.last_will.qos = static_cast<int>(myConfig.lastWillQos);
    config.session.last_will.retain = myConfig.lastWillRetain ? 1 : 0;
    config.network.disable_auto_reconnect = true;
    config.network.timeout_ms = 1000;
    config.outbox.limit = 4096;

    myHandle = esp_mqtt_client_init(&config);
    if (myHandle == nullptr) { return false; }

    if (esp_mqtt_client_register_event(myHandle,
                                       MQTT_EVENT_ANY,
                                       &Esp32s3::mqttEventHandler,
                                       this)
        != ESP_OK)
    {
        esp_mqtt_client_destroy(myHandle);
        myHandle = nullptr;
        return false;
    }

    if (esp_mqtt_client_start(myHandle) != ESP_OK)
    {
        esp_mqtt_client_destroy(myHandle);
        myHandle = nullptr;
        return false;
    }

    myStopping.store(false);
    if (xTaskCreate(&Esp32s3::publishWorker, "mqtt-publish", 4096U, this,
                    tskIDLE_PRIORITY + 1U, &myWorker) != pdPASS)
    {
        esp_mqtt_client_stop(myHandle);
        esp_mqtt_client_destroy(myHandle);
        myHandle = nullptr;
        myConnected.store(false);
        return false;
    }
    myInitialized.store(true);
    return true;
}

bool Esp32s3::reconnect() noexcept
{
    if (!myInitialized.load() || (myHandle == nullptr)) { return false; }
    if (myConnected.load()) { return true; }
    return esp_mqtt_client_reconnect(myHandle) == ESP_OK;
}

void Esp32s3::disconnect() noexcept
{
    myStopping.store(true);
    if (myWorker != nullptr)
    {
        // Shutdown runs only after motor outputs are disabled. Join before
        // destroying the client; never delete a task while it holds its lock.
        xSemaphoreTake(myWorkerDone, portMAX_DELAY);
        myWorker = nullptr;
    }
    if (myHandle != nullptr && myConnected.load()
        && myConfig.lastWillTopic != nullptr && myConfig.lastWillPayload != nullptr)
    {
        // Best effort orderly offline indication; unexpected loss uses the LWT.
        esp_mqtt_client_publish(myHandle, myConfig.lastWillTopic,
                                 myConfig.lastWillPayload, 0, 0,
                                 myConfig.lastWillRetain ? 1 : 0);
    }
    myConnected.store(false);
    myInitialized.store(false);

    if (myHandle != nullptr)
    {
        esp_mqtt_client_stop(myHandle);
        esp_mqtt_client_destroy(myHandle);
        myHandle = nullptr;
    }
    if (myIncomingQueue != nullptr) { xQueueReset(myIncomingQueue); }
    if (myTelemetryQueue != nullptr) { xQueueReset(myTelemetryQueue); }
    if (myStateQueue != nullptr) { xQueueReset(myStateQueue); }
}

bool Esp32s3::isInitialized() const noexcept
{
    return myInitialized.load();
}

bool Esp32s3::isConnected() const noexcept
{
    return myConnected.load();
}

bool Esp32s3::publish(const char* topic,
                      const char* payload,
                      Qos qos,
                      bool retained) noexcept
{
    const auto generation = myGeneration.load();
    if (!myConnected.load() || (myHandle == nullptr) || (topic == nullptr)
        || (payload == nullptr))
    {
        return false;
    }

    Outgoing outgoing{};
    if (myStopping.load()
        || !copyField(outgoing.message.topic, topic, std::strlen(topic))
        || !copyField(outgoing.message.payload, payload, std::strlen(payload))) { return false; }
    outgoing.message.qos = qos;
    outgoing.message.retained = retained;
    outgoing.created = xTaskGetTickCount();
    outgoing.generation = generation;
    if (!myConnected.load() || generation != myGeneration.load()) { return false; }
    // Only fixed-size queue copies run in the control task. ESP-MQTT's own
    // enqueue call takes its client lock, so even that call belongs in a worker.
    return qos == Qos::AtMostOnce
        ? xQueueOverwrite(myTelemetryQueue, &outgoing) == pdTRUE
        : xQueueSend(myStateQueue, &outgoing, 0U) == pdTRUE;
}

bool Esp32s3::subscribe(const char* topic, Qos qos) noexcept
{
    if (myInitialized.load() || (topic == nullptr)
        || (mySubscriptionCount >= mySubscriptions.size()))
    {
        return false;
    }

    auto& subscription = mySubscriptions[mySubscriptionCount];
    const std::size_t length = std::strlen(topic);
    if (!copyField(subscription.topic, topic, length)) { return false; }
    subscription.qos = qos;
    ++mySubscriptionCount;
    return true;
}

bool Esp32s3::readMessage(Message& message) noexcept
{
    return (myIncomingQueue != nullptr)
        && (xQueueReceive(myIncomingQueue, &message, 0U) == pdTRUE);
}

bool Esp32s3::consumeReceiveOverflow() noexcept
{
    return myReceiveOverflow.exchange(false);
}

bool Esp32s3::consumeDisconnect() noexcept
{
    return myDisconnected.exchange(false);
}

void Esp32s3::publishWorker(void* argument)
{
    auto* self = static_cast<Esp32s3*>(argument);
    self->runPublisher();
    xSemaphoreGive(self->myWorkerDone);
    vTaskDelete(nullptr);
}

void Esp32s3::runPublisher() noexcept
{
    Outgoing state{};
    bool pending{false};
    while (!myStopping.load())
    {
        if (!pending) { pending = xQueueReceive(myStateQueue, &state, 0U) == pdTRUE; }
        if (pending)
        {
            if (!myConnected.load() || state.generation != myGeneration.load()) { pending = false; }
            else
            {
                const auto& message = state.message;
                // A full bounded outbox is retried here without blocking control.
                pending = esp_mqtt_client_enqueue(myHandle, message.topic.data(),
                    message.payload.data(), 0, 1, message.retained ? 1 : 0, false) < 0;
            }
        }
        Outgoing telemetry{};
        if (xQueueReceive(myTelemetryQueue, &telemetry, 0U) == pdTRUE
            && myConnected.load() && telemetry.generation == myGeneration.load()
            && (xTaskGetTickCount() - telemetry.created) < pdMS_TO_TICKS(1000U))
        {
            // QoS 0 never enters the retransmission outbox. At most one latest
            // sample waits behind this send; failed/old samples are discarded.
            esp_mqtt_client_publish(myHandle, telemetry.message.topic.data(),
                telemetry.message.payload.data(), 0, 0, telemetry.message.retained ? 1 : 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

void Esp32s3::mqttEventHandler(void* handlerArgs,
                               esp_event_base_t base,
                               std::int32_t eventId,
                               void* eventData)
{
    (void)base;
    auto* self = static_cast<Esp32s3*>(handlerArgs);
    if (self == nullptr) { return; }

    switch (eventId)
    {
        case MQTT_EVENT_CONNECTED:
            self->handleConnected();
            break;

        case MQTT_EVENT_DISCONNECTED:
        case MQTT_EVENT_ERROR:
            self->myConnected.store(false);
            self->myDisconnected.store(true);
            self->myGeneration.fetch_add(1U);
            xQueueReset(self->myIncomingQueue);
            xQueueReset(self->myTelemetryQueue);
            xQueueReset(self->myStateQueue);
            break;

        case MQTT_EVENT_DATA:
            if (eventData != nullptr)
            {
                self->handleData(*static_cast<esp_mqtt_event_handle_t>(eventData));
            }
            break;

        default:
            break;
    }
}

void Esp32s3::handleConnected() noexcept
{
    myConnected.store(true);

    // Subscribing here keeps the potentially blocking ESP-MQTT call in its own task.
    for (std::size_t index{0U}; index < mySubscriptionCount; ++index)
    {
        const auto& subscription = mySubscriptions[index];
        if (esp_mqtt_client_subscribe(myHandle,
                                      subscription.topic.data(),
                                      static_cast<int>(subscription.qos))
            < 0)
        {
            myReceiveOverflow.store(true);
        }
    }
}

void Esp32s3::handleData(const esp_mqtt_event_t& event) noexcept
{
    if ((myIncomingQueue == nullptr) || (event.current_data_offset != 0)
        || (event.total_data_len != event.data_len) || (event.topic_len <= 0)
        || (event.data_len < 0))
    {
        myReceiveOverflow.store(true);
        return;
    }

    Message message{};
    if (!copyField(message.topic,
                   event.topic,
                   static_cast<std::size_t>(event.topic_len))
        || !copyField(message.payload,
                      event.data,
                      static_cast<std::size_t>(event.data_len)))
    {
        myReceiveOverflow.store(true);
        return;
    }

    message.qos = event.qos > 0 ? Qos::AtLeastOnce : Qos::AtMostOnce;
    message.retained = event.retain;
    if (xQueueSend(myIncomingQueue, &message, 0U) != pdTRUE)
    {
        myReceiveOverflow.store(true);
    }
}

template<std::size_t Size>
bool Esp32s3::copyField(std::array<char, Size>& destination,
                        const char* source,
                        std::size_t length) noexcept
{
    if ((source == nullptr) || (length >= Size)) { return false; }
    if (std::memchr(source, '\0', length) != nullptr) { return false; }
    std::memcpy(destination.data(), source, length);
    destination[length] = '\0';
    return true;
}

} // namespace driver::mqtt
