/**
 * @file manager.h
 * @brief Wi-Fi and MQTT communication orchestration for the vehicle.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "system/runtime/control.h"

namespace driver::factory { class Interface; }
namespace driver::mqtt { struct Message; class Interface; }
namespace driver::wifi { class Interface; }

namespace app::communication
{

/** MQTT topics published by the vehicle. Null disables that publication. */
struct PublishTopics
{
    const char* telemetry{nullptr};
    const char* configState{nullptr};
    const char* commandState{nullptr};
    const char* status{nullptr};
};

/** MQTT topics subscribed to by the vehicle. Null disables that subscription. */
struct SubscribeTopics
{
    const char* configSet{nullptr};
    const char* command{nullptr};
};

/** MQTT topic names selected by the application. */
struct Topics
{
    PublishTopics publish{};
    SubscribeTopics subscribe{};
};

/** Vehicle data required for one MQTT telemetry sample. */
struct TelemetrySnapshot
{
    std::array<float, runtime::IrSensorCount> distancesCm{};
    float speedCommand{0.0F};
    float steeringDegrees{0.0F};
    float forwardDuty{0.0F};
    float backwardDuty{0.0F};
    /** Raw counts used for these distances; -1 means unavailable. */
    std::array<std::int32_t, runtime::IrSensorCount> adcRaw{-1, -1, -1};
};

/**
 * @brief Coordinate asynchronous Wi-Fi, MQTT, commands and telemetry.
 *
 * ESP-IDF callbacks remain inside the drivers. The manager is called by the
 * 20 Hz vehicle task and never controls hardware directly.
 */
class Manager final
{
public:
    Manager(driver::factory::Interface& factory, const Topics& topics) noexcept;
    ~Manager() noexcept;
    /** Inject asynchronous drivers for host tests or another platform. */
    Manager(std::unique_ptr<driver::wifi::Interface> wifi,
            std::unique_ptr<driver::mqtt::Interface> mqtt, const Topics& topics) noexcept;

    /** Progress connections and consume a bounded number of messages. */
    void process(std::uint32_t nowMs, runtime::Control& control) noexcept;

    /** Mark command state for publication after a local state transition. */
    void notifyControlStateChanged() noexcept;

    /** Publish telemetry when the configured interval has elapsed. */
    void publishTelemetry(std::uint32_t nowMs,
                          const TelemetrySnapshot& snapshot,
                          const runtime::Control& control) noexcept;

    /** Stop the network clients. */
    void disconnect() noexcept;

    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager(Manager&&) = delete;
    Manager& operator=(Manager&&) = delete;

private:
    void configureSubscriptions() noexcept;
    void flushState(const runtime::Control& control) noexcept;
    void processMqttMessages(std::uint32_t nowMs, runtime::Control& control) noexcept;
    void processMqttMessage(const driver::mqtt::Message& message,
                            std::uint32_t nowMs,
                            runtime::Control& control) noexcept;
    void publishConfigurationState(std::uint32_t requestedRevision,
                                   const char* error,
                                   const runtime::Control& control) noexcept;
    void publishCommandState(const runtime::Control& control) noexcept;
    void setCommandReport(std::uint32_t requestId,
                          const char* result,
                          const char* error) noexcept;

    std::unique_ptr<driver::wifi::Interface> myWifi;
    std::unique_ptr<driver::mqtt::Interface> myMqtt;
    Topics myTopics;

    bool myWifiAttempted{false};
    std::uint32_t myLastWifiAttemptMs{0U};
    std::size_t myWifiRetryIndex{0U};
    bool myMqttAttempted{false};
    std::uint32_t myLastMqttAttemptMs{0U};
    std::size_t myMqttRetryIndex{0U};
    bool myPreviousMqttConnected{false};
    std::uint32_t myLastTelemetryMs{0U};
    std::uint32_t myTelemetrySequence{0U};
    std::uint32_t myLastCommandRequestId{0U};
    const char* myLastCommandResult{"state"};
    const char* myLastCommandError{nullptr};
    bool myCommandStateDirty{true};
    bool myOnlineStateDirty{false};
    bool myConfigStateDirty{false};
    std::uint32_t myPendingConfigRevision{0U};
    const char* myPendingConfigError{nullptr};
};

} // namespace app::communication
