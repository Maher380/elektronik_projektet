/** MQTT integration for the driving methods retained in logic.cpp. */
#include "system/logic/logic.h"
#include "driver/serial/interface.h"
#include "driver/servo/interface.h"

namespace app::logic
{
namespace
{
DriverStyle legacyStyle(app::navigation::DriverStyle style) noexcept
{
    switch (style)
    {
        case app::navigation::DriverStyle::SlowLeft: return DriverStyle::SlowLeft;
        case app::navigation::DriverStyle::SlowRight: return DriverStyle::SlowRight;
        case app::navigation::DriverStyle::GradualSweep: return DriverStyle::GradualSweep;
        case app::navigation::DriverStyle::DecideAction: return DriverStyle::DecideAction;
    }
    return DriverStyle::DecideAction;
}
}

void Logic::initializeMqttOverlay() noexcept
{
    disableMqttOutput();
    if (mySerial && mySerial->isInitialized())
    {
        mySerial->write("SCRUM16 MQTT EXPERIMENT | baseline 83154ef | MQTT stop/duty/style | boot disarmed\n");
    }
}

void Logic::processMqttOverlay(std::uint32_t nowMs) noexcept
{
    // Apply the latest accepted MQTT configuration before the unchanged
    // SCRUM-16 decision code runs for this control tick.
    myCommunication.process(nowMs, myRuntimeControl);
    const auto& config = myRuntimeControl.configuration();
    myStopDistanceCm = config.stopDistanceCm;
    myDriveDuty = config.driveDuty;
    const auto style = legacyStyle(config.driverStyle);
    // Reapplying GradualSweep every tick would reset its original sweep state.
    if (style != myDriverStyle) { setDriverStyle(style); }
}

void Logic::disableMqttOutput() noexcept
{
    // Remove motor power whenever control is lost.
    myCar.disableMotorOutput();
    myMqttAppliedSpeed = 0.0F;
}

bool Logic::authorizeMqttAction(std::uint32_t nowMs) noexcept
{
    // Keep MQTT lease, heartbeat, sensor validity and obstacle state as gates
    // around the original SCRUM-16 actuator call.
    const auto previousControl = myRuntimeControl.controlState();
    const auto previousMotion = myRuntimeControl.motionState();
    const auto previousReason = myRuntimeControl.stateReason();
    const bool authorized = myRuntimeControl.authorizeAction(
        nowMs, hasValidEnvironmentPicture(), myPlannedAction.speed,
        myPlannedAction.stopMode == driver::motor::StopMode::Brake);
    if (authorized)
    {
        myCar.enableMotorOutput();
        myMqttAppliedSpeed = myPlannedAction.speed;
    }
    else
    {
        disableMqttOutput();
    }
    if (previousControl != myRuntimeControl.controlState()
        || previousMotion != myRuntimeControl.motionState()
        || previousReason != myRuntimeControl.stateReason())
    {
        myCommunication.notifyControlStateChanged();
    }
    return authorized;
}

void Logic::publishMqttTelemetry(std::uint32_t nowMs) noexcept
{
    // Publish the latest sensor, command and cached PWM values at the configured rate.
    app::communication::TelemetrySnapshot snapshot{};
    snapshot.distancesCm = {myDistanceToObstacleLeft, myDistanceToObstacleForward,
                            myDistanceToObstacleRight};
    snapshot.speedCommand = myMqttAppliedSpeed;
    const auto* const steering = myCar.steering();
    snapshot.steeringDegrees = steering ? steering->getDirection() : 0.0F;
    myCar.fillPartTelemetry(snapshot);
    myCommunication.publishTelemetry(nowMs, snapshot, myRuntimeControl);
}

void Logic::shutdownMqttOverlay() noexcept
{
    myRuntimeControl.forceDisarm(app::runtime::StateReason::OperatorStop);
    disableMqttOutput();
    myCommunication.disconnect();
}
} // namespace app::logic
