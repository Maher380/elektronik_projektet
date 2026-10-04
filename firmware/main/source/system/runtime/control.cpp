#include "system/runtime/control.h"

#include <algorithm>
#include <cmath>

namespace app::runtime
{
namespace
{
bool isFiniteInRange(float value, float minimum, float maximum) noexcept
{
    return std::isfinite(value) && (value >= minimum) && (value <= maximum);
}
} // namespace

Control::Control(bool systemTest, bool remoteDrivenCar) noexcept
    : mySystemTest{systemTest}, myRemoteDrivenCar{remoteDrivenCar}
{
    if (myRemoteDrivenCar)
    {
        // The operator watches the echoed commands, so telemetry must feel live.
        myConfiguration.driveStyle = navigation::DriveStyle::ManualByRemote;
        myConfiguration.telemetryIntervalMs = 200U;
    }
}

bool Control::supportsDriveStyle(navigation::DriveStyle style) const noexcept
{
    // A remote-driven car has no obstacle distances, so the autonomous styles cannot run
    // on it. This is about the car, not about what is selected now.
    if (myRemoteDrivenCar)
    {
        return (style == navigation::DriveStyle::ManualByRemote)
            || (style == navigation::DriveStyle::GapCalibration);
    }
    switch (style)
    {
        case navigation::DriveStyle::DecideAction: return true;
        case navigation::DriveStyle::SlowLeft:
        case navigation::DriveStyle::SlowRight:
        case navigation::DriveStyle::GradualSweep: return !mySystemTest;
        case navigation::DriveStyle::ManualByRemote:
        // GapCalibration measures a wheel's magnet gaps, so it needs an Odometer with
        // magnets on it. Only a remote-driven car has one.
        case navigation::DriveStyle::GapCalibration: return false;
    }
    return false;
}

ConfigurationResult Control::applyConfiguration(
    const ConfigurationRequest& request) noexcept
{
    if (request.revision == 0U)
    {
        return ConfigurationResult::InvalidRevision;
    }
    if (!isFiniteInRange(request.values.stopDistanceCm, mySystemTest ? 1.0F : 30.0F, mySystemTest ? 100.0F : 70.0F))
    {
        return ConfigurationResult::StopDistanceOutOfRange;
    }
    if (!isFiniteInRange(request.values.driveDuty, 0.0F, 1.0F))
    {
        return ConfigurationResult::DriveDutyOutOfRange;
    }
    if ((request.values.telemetryIntervalMs < 200U)
        || (request.values.telemetryIntervalMs > 5000U))
    {
        return ConfigurationResult::TelemetryIntervalOutOfRange;
    }

    Configuration candidate = request.values;
    if (!request.hasReactionDistance) { candidate.reactionDistanceCm = myConfiguration.reactionDistanceCm; }
    if (!request.hasLoopInterval) { candidate.loopIntervalMs = myConfiguration.loopIntervalMs; }
    if (!isFiniteInRange(candidate.reactionDistanceCm, 1.0F, 200.0F)
        || (mySystemTest && candidate.reactionDistanceCm <= candidate.stopDistanceCm))
    {
        return ConfigurationResult::ReactionDistanceOutOfRange;
    }
    if (candidate.loopIntervalMs < 20U || candidate.loopIntervalMs > 1000U)
    {
        return ConfigurationResult::LoopIntervalOutOfRange;
    }
    if (!request.hasDriveStyle) { candidate.driveStyle = myConfiguration.driveStyle; }
    if (!supportsDriveStyle(candidate.driveStyle)) { return ConfigurationResult::InvalidDriveStyle; }
    // Reject the entire update: neither values nor revision may advance.
    if (candidate.driveStyle != myConfiguration.driveStyle && myControlState != ControlState::Disarmed)
    {
        return ConfigurationResult::DriveStyleRequiresDisarmed;
    }

    if (myHasConfigurationRevision)
    {
        if ((request.revision == myConfigurationRevision)
            && sameConfiguration(candidate, myConfiguration))
        {
            return ConfigurationResult::Duplicate;
        }
        if (request.revision <= myConfigurationRevision)
        {
            return ConfigurationResult::StaleRevision;
        }
    }

    myConfiguration = candidate;
    myConfigurationRevision = request.revision;
    myHasConfigurationRevision = true;
    return ConfigurationResult::Applied;
}

bool Control::setDriveStyle(navigation::DriveStyle style) noexcept
{
    if (!supportsDriveStyle(style)) { return false; }
    if (style != myConfiguration.driveStyle && myControlState != ControlState::Disarmed) { return false; }
    myConfiguration.driveStyle = style;
    return true;
}

void Control::setMqttConnected(bool connected) noexcept
{
    if (myMqttConnected && !connected && (myControlState == ControlState::Armed))
    {
        disarm(StateReason::MqttDisconnected);
    }
    myMqttConnected = connected;
}

CommandResult Control::handleCommand(const Command& command,
                                     std::uint32_t nowMs) noexcept
{
    if (command.sessionId[0] == '\0')
    {
        return {false, CommandError::InvalidSession};
    }

    switch (command.type)
    {
        case CommandType::Start:
            if (myActuatorFault) { return {false, CommandError::NotArmed}; }
            if (!command.hasRequestId || command.requestId == 0U)
            {
                return {false, CommandError::InvalidRequest};
            }
            if (!myMqttConnected)
            {
                return {false, CommandError::MqttDisconnected};
            }

            if (command.requestId <= myLastControlRequestId)
            {
                // A QoS-1 retransmission may be acknowledged, but never renews
                // a lease or re-arms a stopped session.
                if (command.requestId == myLastStartRequestId
                    && myControlState == ControlState::Armed
                    && isActiveSession(command.sessionId)
                    && (nowMs - myLastHeartbeatMs) < HeartbeatTimeoutMs)
                {
                    return {true, CommandError::None};
                }
                return {false, CommandError::StaleRequest};
            }
            myLastControlRequestId = command.requestId;
            myLastStartRequestId = command.requestId;
            myServoTest = false;
            // A new session never inherits the previous session's drive commands.
            myDrive = {};
            myHasDrive = false;
            myActiveSession = command.sessionId;
            myLastHeartbeatMs = nowMs;
            myControlState = ControlState::Armed;
            myMotionState = MotionState::Stopped;
            myStateReason = StateReason::None;
            return {true, CommandError::None};

        case CommandType::Stop:
            myLastControlRequestId = std::max(myLastControlRequestId, command.requestId);
            disarm(StateReason::OperatorStop);
            return {true, CommandError::None};

        case CommandType::Servo:
            if (!mySystemTest || myActuatorFault || !command.hasRequestId
                || command.requestId == 0U
                || !isFiniteInRange(command.servoAngleDegrees, -90.0F, 90.0F))
            {
                return {false, CommandError::InvalidRequest};
            }
            if (!myMqttConnected) { return {false, CommandError::MqttDisconnected}; }
            if (command.requestId <= myLastControlRequestId) { return {false, CommandError::StaleRequest}; }
            myLastControlRequestId = command.requestId;
            disarm(StateReason::OperatorStop);
            myServoAngleDegrees = command.servoAngleDegrees;
            myServoTest = true;
            return {true, CommandError::None};

        case CommandType::Heartbeat:
            if (myControlState == ControlState::Armed
                && (nowMs - myLastHeartbeatMs) >= HeartbeatTimeoutMs)
            {
                disarm(StateReason::HeartbeatTimeout);
            }
            if (myControlState != ControlState::Armed)
            {
                return {false, CommandError::NotArmed};
            }
            if (!isActiveSession(command.sessionId))
            {
                return {false, CommandError::SessionMismatch};
            }

            myLastHeartbeatMs = nowMs;
            return {true, CommandError::None};

        case CommandType::StoreGaps:
            // Confirmed while disarmed: the run that measured the table disarmed the car
            // on its way to finishing, so there is no lease to check here.
            if (!command.hasRequestId || command.requestId == 0U)
            {
                return {false, CommandError::InvalidRequest};
            }
            if (!myMqttConnected) { return {false, CommandError::MqttDisconnected}; }
            if (myControlState != ControlState::Disarmed)
            {
                // Storing mid-run would confirm a table the run is still measuring.
                return {false, CommandError::InvalidRequest};
            }
            if (command.requestId <= myLastControlRequestId)
            {
                // A QoS-1 retransmission is acknowledged but must not store twice.
                return {true, CommandError::None};
            }
            myLastControlRequestId = command.requestId;
            myStoreGapsRequested = true;
            return {true, CommandError::None};

        case CommandType::Drive:
            // Keyed on the selected style, not on the car: a Drive command must be
            // refused while a measurement style is driving its own script, or the
            // operator's sliders would be injected into a run already in progress.
            if (!isManualByRemoteStyle() || myActuatorFault
                || !isFiniteInRange(command.steeringCommand, -90.0F, 90.0F)
                || !isFiniteInRange(command.speedCommand, -100.0F, 100.0F))
            {
                return {false, CommandError::InvalidRequest};
            }
            if (myControlState == ControlState::Armed
                && (nowMs - myLastHeartbeatMs) >= HeartbeatTimeoutMs)
            {
                disarm(StateReason::HeartbeatTimeout);
            }
            if (myControlState != ControlState::Armed) { return {false, CommandError::NotArmed}; }
            if (!isActiveSession(command.sessionId)) { return {false, CommandError::SessionMismatch}; }

            // Each accepted drive command also counts as an operator heartbeat.
            myDrive = {command.steeringCommand, command.speedCommand};
            myHasDrive = true;
            myLastDriveMs = nowMs;
            myLastHeartbeatMs = nowMs;
            return {true, CommandError::None};
    }

    return {false, CommandError::InvalidSession};
}

void Control::forceDisarm(StateReason reason) noexcept
{
    if (reason == StateReason::ActuatorFault) { myActuatorFault = true; }
    disarm(reason);
}

const char* Control::activeSessionId() const noexcept
{
    return myActiveSession.data();
}

const Configuration& Control::configuration() const noexcept
{
    return myConfiguration;
}

std::uint32_t Control::configurationRevision() const noexcept
{
    return myConfigurationRevision;
}

bool Control::hasConfigurationRevision() const noexcept
{
    return myHasConfigurationRevision;
}

bool Control::isMqttConnected() const noexcept
{
    return myMqttConnected;
}

ControlState Control::controlState() const noexcept
{
    return myControlState;
}

MotionState Control::motionState() const noexcept
{
    return myMotionState;
}

StateReason Control::stateReason() const noexcept
{
    return myStateReason;
}

bool Control::sameConfiguration(const Configuration& lhs,
                                const Configuration& rhs) noexcept
{
    return (lhs.stopDistanceCm == rhs.stopDistanceCm)
        && (lhs.reactionDistanceCm == rhs.reactionDistanceCm)
        && (lhs.loopIntervalMs == rhs.loopIntervalMs)
        && (lhs.driveDuty == rhs.driveDuty)
        && (lhs.telemetryIntervalMs == rhs.telemetryIntervalMs)
        && (lhs.driveStyle == rhs.driveStyle);
}

bool Control::isActiveSession(
    const std::array<char, SessionIdSize>& sessionId) const noexcept
{
    return sessionId == myActiveSession;
}

void Control::disarm(StateReason reason) noexcept
{
    myServoTest = false;
    myControlState = ControlState::Disarmed;
    myMotionState = MotionState::Stopped;
    myStateReason = myActuatorFault ? StateReason::ActuatorFault : reason;
    myActiveSession.fill('\0');
    myLastHeartbeatMs = 0U;
    myDrive = {};
    myHasDrive = false;
}

void Control::finishDriveStyle() noexcept
{
    // A completed run is not a fault, so this reports its own reason rather than borrowing
    // one from the fault channel. The car is left ready for the next Start.
    disarm(StateReason::DriveStyleFinished);
}

bool Control::takeStoreGapsRequest() noexcept
{
    const bool requested{myStoreGapsRequested};
    myStoreGapsRequested = false;
    return requested;
}

bool Control::renewLease(const std::uint32_t nowMs) noexcept
{
    if (myControlState != ControlState::Armed) { return false; }
    if (!myMqttConnected)
    {
        disarm(StateReason::MqttDisconnected);
        return false;
    }
    if ((nowMs - myLastHeartbeatMs) >= HeartbeatTimeoutMs)
    {
        disarm(StateReason::HeartbeatTimeout);
        return false;
    }
    return true;
}

RemoteDrive Control::remoteDrive(std::uint32_t nowMs, std::uint32_t driveTimeoutMs) noexcept
{
    // Keyed on the selected style: only the operator-driven style has operator commands
    // to return. Any other style gets nothing here and drives from its own decision.
    if (!isManualByRemoteStyle())
    {
        myMotionState = MotionState::Stopped;
        return {};
    }
    // The lease is style-independent; renewLease() disarms on a lost connection or a
    // stale heartbeat, and disarm() has already set the motion state when it does.
    if (!renewLease(nowMs))
    {
        myMotionState = MotionState::Stopped;
        return {};
    }
    if (!myHasDrive)
    {
        // Armed, but the operator has not sent a drive command in this session yet.
        myMotionState = MotionState::Stopped;
        myStateReason = StateReason::None;
        return {};
    }
    if ((nowMs - myLastDriveMs) >= driveTimeoutMs)
    {
        // Keep steering where it was and let the car roll; resume on the next command.
        myMotionState = MotionState::Inhibited;
        myStateReason = StateReason::DriveTimeout;
        return {myDrive.steeringCommand, 0.0F};
    }
    myMotionState = myDrive.speedCommand != 0.0F ? MotionState::Moving : MotionState::Stopped;
    myStateReason = StateReason::None;
    return myDrive;
}

bool Control::authorizeAction(std::uint32_t nowMs, bool validEnvironment,
                              float plannedDuty, bool brakeRequested) noexcept
{
    // MQTT lease and safety state gate actuator output after Logic has planned
    // the action; this method never chooses a route or changes the duty.
    if (myControlState != ControlState::Armed)
    {
        myMotionState = MotionState::Stopped;
        return false;
    }
    if (!myMqttConnected)
    {
        disarm(StateReason::MqttDisconnected);
        return false;
    }
    if ((nowMs - myLastHeartbeatMs) >= HeartbeatTimeoutMs)
    {
        disarm(StateReason::HeartbeatTimeout);
        return false;
    }

    if (!validEnvironment)
    {
        myMotionState = MotionState::Inhibited;
        myStateReason = StateReason::SensorFault;
    }
    else if (brakeRequested)
    {
        myMotionState = MotionState::Inhibited;
        myStateReason = StateReason::Obstacle;
    }
    else if (plannedDuty <= 0.0F)
    {
        myMotionState = MotionState::Stopped;
        myStateReason = StateReason::None;
    }
    else
    {
        myMotionState = MotionState::Moving;
        myStateReason = StateReason::None;
    }
    return true;
}

} // namespace app::runtime
