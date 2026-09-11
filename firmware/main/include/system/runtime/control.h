/**
 * @file control.h
 * @brief Runtime safety state for MQTT-controlled autonomous driving.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "system/navigation/planner.h"

namespace app::runtime
{

inline constexpr std::size_t IrSensorCount{3U};
inline constexpr std::size_t SessionIdSize{33U};

/** Runtime values that may be changed through MQTT. */
struct Configuration
{
    float stopDistanceCm{30.0F};
    float driveDuty{0.5F};
    std::uint32_t telemetryIntervalMs{1000U};
    navigation::DriverStyle driverStyle{navigation::DriverStyle::DecideAction};
};

/** A complete, versioned runtime configuration request. */
struct ConfigurationRequest
{
    std::uint32_t revision{0U};
    Configuration values{};
    /** Legacy MQTT payloads omit the style and preserve the active RAM value. */
    bool hasDriverStyle{true};
};

/** Result of applying a runtime configuration request. */
enum class ConfigurationResult : std::uint8_t
{
    Applied,
    Duplicate,
    InvalidRevision,
    StopDistanceOutOfRange,
    DriveDutyOutOfRange,
    TelemetryIntervalOutOfRange,
    StaleRevision,
    InvalidDriverStyle,
    DriverStyleRequiresDisarmed,
};

/** Supported transient MQTT commands. */
enum class CommandType : std::uint8_t
{
    Start,
    Stop,
    Heartbeat,
};

/** Parsed transient command. */
struct Command
{
    CommandType type{CommandType::Stop};
    std::uint32_t requestId{0U};
    bool hasRequestId{false};
    std::array<char, SessionIdSize> sessionId{};
};

/** Reason why a parsed command was rejected. */
enum class CommandError : std::uint8_t
{
    None,
    MqttDisconnected,
    InvalidSession,
    NotArmed,
    SessionMismatch,
    InvalidRequest,
    StaleRequest,
};

/** Command handling result returned to the MQTT protocol layer. */
struct CommandResult
{
    bool accepted{false};
    CommandError error{CommandError::None};
};

/** Operator authorization state. */
enum class ControlState : std::uint8_t
{
    Disarmed,
    Armed,
};

/** Current physical motion decision. */
enum class MotionState : std::uint8_t
{
    Stopped,
    Moving,
    Inhibited,
};

/** Reason associated with the current control and motion state. */
enum class StateReason : std::uint8_t
{
    Boot,
    OperatorStop,
    Obstacle,
    SensorFault,
    HeartbeatTimeout,
    MqttDisconnected,
    MessageOverflow,
    ActuatorFault,
    None,
};

/**
 * @brief Own the safety-critical runtime state independently of MQTT JSON.
 *
 * The class has no ESP-IDF dependencies so its state transitions can be tested
 * on the host. Only the vehicle control loop should mutate an instance.
 */
class Control final
{
public:
    static constexpr std::uint32_t HeartbeatTimeoutMs{3000U};

    /** Apply a complete runtime configuration atomically. */
    ConfigurationResult applyConfiguration(const ConfigurationRequest& request) noexcept;

    /** Select a local style while disarmed; does not allocate a MQTT revision. */
    bool setDriverStyle(navigation::DriverStyle style) noexcept;

    /** Update whether the MQTT control connection is currently available. */
    void setMqttConnected(bool connected) noexcept;

    /** Handle one parsed start, stop, or heartbeat command. */
    CommandResult handleCommand(const Command& command, std::uint32_t nowMs) noexcept;

    /** Force a fail-safe disarm for a local runtime error. */
    void forceDisarm(StateReason reason) noexcept;

    /** Check only the MQTT lease and report SCRUM-16's already-made decision.
     * Returns true even for a sensor/obstacle stop, so original executeAction()
     * applies SCRUM-16's original brake mode. Does not choose a path or duty.
     */
    bool authorizeScrum16Action(std::uint32_t nowMs, bool validEnvironment,
                               float plannedDuty, bool brakeRequested) noexcept;

    const Configuration& configuration() const noexcept;
    std::uint32_t configurationRevision() const noexcept;
    bool hasConfigurationRevision() const noexcept;
    bool isMqttConnected() const noexcept;
    ControlState controlState() const noexcept;
    MotionState motionState() const noexcept;
    StateReason stateReason() const noexcept;
    const char* activeSessionId() const noexcept;

private:
    static bool sameConfiguration(const Configuration& lhs,
                                  const Configuration& rhs) noexcept;
    bool isActiveSession(const std::array<char, SessionIdSize>& sessionId) const noexcept;
    void disarm(StateReason reason) noexcept;

    Configuration myConfiguration{};
    std::uint32_t myConfigurationRevision{0U};
    bool myHasConfigurationRevision{false};
    bool myMqttConnected{false};
    ControlState myControlState{ControlState::Disarmed};
    MotionState myMotionState{MotionState::Stopped};
    StateReason myStateReason{StateReason::Boot};
    std::array<char, SessionIdSize> myActiveSession{};
    std::uint32_t myLastHeartbeatMs{0U};
    // A RAM high-water mark rejects old starts across stop/reconnect. IDs are
    // globally increasing during this boot; stop is always honored.
    std::uint32_t myLastControlRequestId{0U};
    std::uint32_t myLastStartRequestId{0U};
    bool myActuatorFault{false};
};

} // namespace app::runtime
