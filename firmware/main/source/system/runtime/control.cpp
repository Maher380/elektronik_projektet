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
bool isValidDriverStyle(navigation::DriverStyle style) noexcept
{
    switch (style)
    {
        case navigation::DriverStyle::DecideAction:
        case navigation::DriverStyle::SlowLeft:
        case navigation::DriverStyle::SlowRight:
        case navigation::DriverStyle::GradualSweep: return true;
    }
    return false;
}
} // namespace

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
    if (!request.hasDriverStyle) { candidate.driverStyle = myConfiguration.driverStyle; }
    if ((mySystemTest && candidate.driverStyle != navigation::DriverStyle::DecideAction) || !isValidDriverStyle(candidate.driverStyle)) { return ConfigurationResult::InvalidDriverStyle; }
    // Reject the entire update: neither values nor revision may advance.
    if (candidate.driverStyle != myConfiguration.driverStyle && myControlState != ControlState::Disarmed)
    {
        return ConfigurationResult::DriverStyleRequiresDisarmed;
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

bool Control::setDriverStyle(navigation::DriverStyle style) noexcept
{
    if ((mySystemTest && style != navigation::DriverStyle::DecideAction) || !isValidDriverStyle(style)) { return false; }
    if (style != myConfiguration.driverStyle && myControlState != ControlState::Disarmed) { return false; }
    myConfiguration.driverStyle = style;
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
        && (lhs.driverStyle == rhs.driverStyle);
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
