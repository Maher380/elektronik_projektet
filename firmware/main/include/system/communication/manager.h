/**
 * @file manager.h
 * @brief Wi-Fi and MQTT communication orchestration for the vehicle.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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

/** Car network and broker login selected by the application. Pointers must outlive the manager. */
struct NetworkSettings
{
    const char* wifiSsid{nullptr};
    const char* wifiPassword{nullptr};
    const char* mqttBrokerUri{nullptr};
    const char* mqttClientId{nullptr};
    const char* mqttUsername{nullptr};
    const char* mqttPassword{nullptr};
};

/** Network settings from idf.py menuconfig; null where Wi-Fi or MQTT is disabled. */
NetworkSettings kconfigNetworkSettings() noexcept;

/**
 * @brief Ford SpeedCalibration's progress and its latest finished leg.
 *
 * Sent as a nested "speed_calibration" object, present only while that drive style is
 * selected. Only the latest leg travels, not the whole table: the payload is 1024 bytes
 * and is already most of the way there, so the console collects the legs as they finish.
 * Optional values are left out when NaN.
 */
struct SpeedCalibrationTelemetry
{
    /** Where the run has got to, e.g. "driving". nullptr leaves the whole object out. */
    const char* phase{nullptr};
    /** Why the run ended early, e.g. "stopped"; nullptr leaves it out. */
    const char* failure{nullptr};
    /** Leg being driven, and how many the recipe has. */
    std::uint8_t leg{0U};
    std::uint8_t legCount{0U};
    /** Length of every leg, start to standstill. */
    float legM{0.0F};
    /** The leg being driven: first and last target speed (0 = none), and direction. */
    float fromMs{0.0F};
    float targetMs{0.0F};
    bool forward{true};
    /** Learned stopping distance factor k in k * v^2. */
    float stopK{0.0F};

    /** Outcome of the latest finished leg, e.g. "reached"; nullptr leaves "last" out. */
    const char* lastResult{nullptr};
    std::uint8_t lastLeg{0U};
    float lastFromMs{0.0F};
    float lastTargetMs{0.0F};
    bool lastForward{true};
    float lastSpeedMs{std::numeric_limits<float>::quiet_NaN()};
    float lastDuty{std::numeric_limits<float>::quiet_NaN()};
    float lastRiseS{std::numeric_limits<float>::quiet_NaN()};
    float lastOvershootMs{std::numeric_limits<float>::quiet_NaN()};
    float lastStopM{0.0F};
    float lastDistanceM{0.0F};
};

/** Vehicle data required for one MQTT telemetry sample. */
struct TelemetrySnapshot
{
    std::array<float, runtime::IrSensorCount> distancesCm{};
    /** Exact distances used by main.cpp after applying the reaction cap. */
    std::array<float, runtime::IrSensorCount> decisionDistancesCm{};
    /** Vagrant: motor duty 0–1. Ford: applied speed command −100 to +100. */
    float speedCommand{0.0F};
    float steeringDegrees{0.0F};
    float forwardDuty{0.0F};
    float backwardDuty{0.0F};
    /** Ford: what the motor is doing, e.g. "driving_forward"; nullptr leaves it out. */
    const char* motorState{nullptr};
    /** Ford: drive battery voltage in Volts; NaN leaves it out. */
    float batteryVoltage{std::numeric_limits<float>::quiet_NaN()};
    /** Ford: speed measured by the Odometer in m/s, not a command; NaN leaves it out. */
    float measuredSpeedMs{std::numeric_limits<float>::quiet_NaN()};
    /** Ford: distance the Odometer has counted since boot in meters; NaN leaves it out. */
    float odometerDistanceM{std::numeric_limits<float>::quiet_NaN()};
    /** Ford: what the measured speed was worked out from; nullptr leaves it out. */
    const char* measuredSpeedSource{nullptr};
    /** Ford: times the Odometer has given up a recovered magnet phase. */
    std::uint32_t odometerPhaseLosses{0U};
    /** Ford: motor can temperature in degrees Celsius; NaN leaves it out. */
    float motorTemperatureC{std::numeric_limits<float>::quiet_NaN()};
    /** Ford: steering servo temperature in degrees Celsius; NaN leaves it out. */
    float servoTemperatureC{std::numeric_limits<float>::quiet_NaN()};

    // GapCalibration. These travel as a nested "calibration" object, present only while
    // that drive style is selected, which is how the payload already treats optional
    // groups. See ADR 0009.
    /** Where the run has got to, e.g. "sampling". nullptr leaves the whole object out. */
    const char* calibrationPhase{nullptr};
    /** Why the last run produced no table, e.g. "stalled"; nullptr leaves it out. */
    const char* calibrationFailure{nullptr};
    /** Which duty of the recipe is being measured, and how many there are. */
    std::uint8_t calibrationDutyIndex{0U};
    std::uint8_t calibrationDutyCount{0U};
    /** Revolutions averaged at this duty so far, and how many are wanted. */
    std::uint8_t calibrationSamples{0U};
    std::uint8_t calibrationRevolutions{0U};
    /** Measured gap fractions, or nullptr when no table is waiting to be confirmed. */
    const float* calibrationGaps{nullptr};
    std::uint8_t calibrationGapCount{0U};
    /** Largest disagreement between speeds; NaN leaves it out. */
    float calibrationSpread{std::numeric_limits<float>::quiet_NaN()};
    /** Distance to the next-best rotation; NaN leaves it out. */
    float calibrationMargin{std::numeric_limits<float>::quiet_NaN()};
    /** Whether the operator has had the measured table written to NVS. */
    bool calibrationStored{false};
    /** Whether a store was attempted and failed. */
    bool calibrationStoreFailed{false};
    /**
     * @brief Whether the motor overheat guard is active during a run.
     *
     * False means the temperature sensor is absent or failed and the run is going ahead
     * without that guard, which ADR 0009 accepts deliberately. The operator is told.
     */
    bool calibrationOverheatGuard{false};
    /**
     * Ford: why safe mode selected Disabled, "motor_temp" or "servo_temp". Sent as a nested
     * "disabled" object only while that style is selected; nullptr leaves it out.
     */
    const char* disabledCause{nullptr};
    /** Ford: the temperature that triggered safe mode, in degrees Celsius. */
    float disabledTemperatureC{std::numeric_limits<float>::quiet_NaN()};
    /** Ford: SpeedCalibration; see SpeedCalibrationTelemetry. */
    SpeedCalibrationTelemetry speedCalibration{};
    /** Raw counts used for these distances; -1 means unavailable. */
    std::array<std::int32_t, runtime::IrSensorCount> adcRaw{-1, -1, -1};
};

/**
 * @brief Coordinate asynchronous Wi-Fi, MQTT, commands and telemetry.
 *
 * ESP-IDF callbacks remain inside the drivers. The manager is called by the
 * vehicle task and never controls hardware directly.
 */
class Manager final
{
public:
    Manager(driver::factory::Interface& factory, const Topics& topics,
            const NetworkSettings& network) noexcept;
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
