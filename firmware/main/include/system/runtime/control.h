/**
 * @file control.h
 * @brief Runtime safety state for MQTT-controlled autonomous driving.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "system/navigation/types.h"

namespace app::runtime
{

inline constexpr std::size_t IrSensorCount{3U};
inline constexpr std::size_t SessionIdSize{33U};

/** Runtime values that may be changed through MQTT. */
struct Configuration
{
    float stopDistanceCm{30.0F};
    /** Upper decision distance; non-finite readings also use this value. */
    float reactionDistanceCm{40.0F};
    /** Sensor and navigation period; networking is serviced independently. */
    std::uint32_t loopIntervalMs{20U};
    float driveDuty{0.5F};
    std::uint32_t telemetryIntervalMs{1000U};
    navigation::DriveStyle driveStyle{navigation::DriveStyle::DecideAction};
};

/** A complete, versioned runtime configuration request. */
struct ConfigurationRequest
{
    std::uint32_t revision{0U};
    Configuration values{};
    /** Legacy MQTT payloads omit the style and preserve the active RAM value. */
    bool hasDriveStyle{true};
    /** Omitted extension fields preserve the current values. */
    bool hasReactionDistance{true};
    bool hasLoopInterval{true};
};

/** Result of applying a runtime configuration request. */
enum class ConfigurationResult : std::uint8_t
{
    Applied,
    Duplicate,
    InvalidRevision,
    StopDistanceOutOfRange,
    DriveDutyOutOfRange,
    ReactionDistanceOutOfRange,
    LoopIntervalOutOfRange,
    TelemetryIntervalOutOfRange,
    StaleRevision,
    InvalidDriveStyle,
    DriveStyleRequiresDisarmed,
};

/** Supported transient MQTT commands. */
enum class CommandType : std::uint8_t
{
    Start,
    Stop,
    Heartbeat,
    Servo,
    /** ManualByRemote steering and speed commands; also renews the heartbeat. */
    Drive,
    /**
     * @brief GapCalibration: store the measured gap table the car is holding.
     *
     * Accepted only while disarmed, unlike the drive commands, because the run that
     * produced the table disarmed the car on its way to finishing. See ADR 0009.
     */
    StoreGaps,
};

/** Parsed transient command. */
struct Command
{
    CommandType type{CommandType::Stop};
    std::uint32_t requestId{0U};
    bool hasRequestId{false};
    /** Requested servo angle for the transient Servo command only. */
    float servoAngleDegrees{0.0F};
    /** Drive only: −90 full left, 0 straight ahead, +90 full right. */
    float steeringCommand{0.0F};
    /** Drive only: −100 full reverse, 0 no drive, +100 full forward. */
    float speedCommand{0.0F};
    std::array<char, SessionIdSize> sessionId{};
};

/** The operator's latest drive commands that a ManualByRemote car should follow now. */
struct RemoteDrive
{
    /** Steering command to apply; 0 while disarmed. */
    float steeringCommand{0.0F};
    /** Speed command to apply; 0 (no drive) while disarmed or after a drive timeout. */
    float speedCommand{0.0F};
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
    /** ManualByRemote: drive commands stopped arriving; no drive, still armed. */
    DriveTimeout,
    /** The selected drive style ran to its end. Not a fault; Start begins another run. */
    DriveStyleFinished,
    /** Ford safe mode: a temperature reached its limit, so the car switched to Disabled. */
    Overheated,
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
    /**
     * @param systemTest Enable the main.cpp system test protocol; legacy Logic stays compatible.
     * @param remoteDrivenCar Ford: a car with no obstacle distances, so none of the
     *        autonomous drive styles apply to it. A property of the car, fixed for its
     *        life - not of whichever drive style happens to be selected.
     */
    explicit Control(bool systemTest = false, bool remoteDrivenCar = false) noexcept;
    /** Whether the active application implements the main.cpp system test. */
    bool isSystemTest() const noexcept { return mySystemTest; }
    /** Whether this car has no obstacle distances; see the constructor. */
    bool isRemoteDrivenCar() const noexcept { return myRemoteDrivenCar; }
    /** Whether the drive style selected right now is the operator-driven one. */
    bool isManualByRemoteStyle() const noexcept
    {
        return myConfiguration.driveStyle == navigation::DriveStyle::ManualByRemote;
    }
    /** True while a manual servo command owns steering with the motor disabled. */
    bool isServoTest() const noexcept { return myServoTest; }
    /** Angle requested by the most recently accepted manual servo command. */
    float servoAngleDegrees() const noexcept { return myServoAngleDegrees; }

    static constexpr std::uint32_t HeartbeatTimeoutMs{3000U};

    /** Apply a complete runtime configuration atomically. */
    ConfigurationResult applyConfiguration(const ConfigurationRequest& request) noexcept;

    /** Select a local style while disarmed; does not allocate a MQTT revision. */
    bool setDriveStyle(navigation::DriveStyle style) noexcept;

    /** Update whether the MQTT control connection is currently available. */
    void setMqttConnected(bool connected) noexcept;

    /** Handle one parsed start, stop, heartbeat, or manual servo command. */
    CommandResult handleCommand(const Command& command, std::uint32_t nowMs) noexcept;

    /** Force a fail-safe disarm for a local runtime error. */
    void forceDisarm(StateReason reason) noexcept;

    /**
     * @brief End the selected drive style's run, and disarm because it is over.
     *
     * A style that exists to finish something - a measurement, a lap - reaches an end on
     * its own, and reaching it leaves the car ready to be started again. This is not a
     * fail-safe disarm: forceDisarm() is for a runtime error, and a completed measurement
     * is not one. See ADR 0009.
     */
    void finishDriveStyle() noexcept;

    /**
     * @brief Take a pending request to store a measured gap table.
     *
     * @return True once per accepted StoreGaps command; the request is consumed by reading
     *         it, so the logic layer acts on it exactly once.
     */
    bool takeStoreGapsRequest() noexcept;

    /**
     * @brief Enforce the operator's lease, whatever drive style is selected.
     *
     * Disarms on a lost MQTT connection or a heartbeat timeout. remoteDrive() applies this
     * for ManualByRemote, but a style that drives a script of its own never calls
     * remoteDrive, and a console that goes quiet while TCP stays up would otherwise leave
     * a powered wheel turning for as long as the script runs. Every drive style's loop
     * must reach this once a tick.
     *
     * @param[in] nowMs Monotonic milliseconds.
     * @return True if the car is still armed afterwards.
     */
    bool renewLease(std::uint32_t nowMs) noexcept;

    /** Check only the MQTT lease and report SCRUM-16's already-made decision.
     * Returns true even for a sensor/obstacle stop, so original executeAction()
     * applies SCRUM-16's original brake mode. Does not choose a path or duty.
     */
    bool authorizeAction(std::uint32_t nowMs, bool validEnvironment,
                               float plannedDuty, bool brakeRequested) noexcept;

    /**
     * @brief ManualByRemote: check the operator lease and return the commands to follow now.
     *
     * Disarms on a lost MQTT connection or heartbeat timeout. While armed, no drive
     * command for driveTimeoutMs gives no drive but keeps the car armed; the next
     * drive command resumes driving.
     * @todo Add host tests for the lease, drive timeout and session rules.
     */
    RemoteDrive remoteDrive(std::uint32_t nowMs, std::uint32_t driveTimeoutMs) noexcept;

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
    bool supportsDriveStyle(navigation::DriveStyle style) const noexcept;
    void disarm(StateReason reason) noexcept;

    const bool mySystemTest;
    const bool myRemoteDrivenCar;
    /** Latest accepted Drive command; cleared on start and disarm. */
    RemoteDrive myDrive{};
    bool myHasDrive{false};
    std::uint32_t myLastDriveMs{0U};
    bool myServoTest{false};
    /** An accepted StoreGaps command waiting for the logic layer to act on it. */
    bool myStoreGapsRequested{false};
    float myServoAngleDegrees{0.0F};
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
