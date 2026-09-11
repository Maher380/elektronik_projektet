/** MQTT integration for the SCRUM-16 driving methods retained in logic.cpp. */
#include "system/logic/logic.h"
#include "driver/serial/interface.h"

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
    if (myMotorSleep) { myMotorSleep->write(false); }
    if (myMotorForwardsPwm) { myMotorForwardsPwm->setDuty(0.0F); }
    if (myMotorBackwardsPwm) { myMotorBackwardsPwm->setDuty(0.0F); }
    myMqttAppliedSpeed = 0.0F;
}

bool Logic::authorizeMqttAction(std::uint32_t nowMs) noexcept
{
    const auto previousControl = myRuntimeControl.controlState();
    const auto previousMotion = myRuntimeControl.motionState();
    const auto previousReason = myRuntimeControl.stateReason();
    const bool authorized = myRuntimeControl.authorizeScrum16Action(
        nowMs, hasValidEnvironmentPicture(), myPlannedAction.speed,
        myPlannedAction.stopMode == driver::motor::StopMode::Brake);
    if (authorized)
    {
        myMotorSleep->write(true);
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
    app::communication::TelemetrySnapshot snapshot{};
    snapshot.distancesCm = {myDistanceToObstacleLeft, myDistanceToObstacleForward,
                            myDistanceToObstacleRight};
    snapshot.adcRaw = {myIrSensorLeftAdc ? myIrSensorLeftAdc->lastRaw() : -1,
                       myIrSensorForwardAdc ? myIrSensorForwardAdc->lastRaw() : -1,
                       myIrSensorRightAdc ? myIrSensorRightAdc->lastRaw() : -1};
    snapshot.speedCommand = myMqttAppliedSpeed;
    snapshot.steeringDegrees = mySteeringServo ? mySteeringServo->getDirection() : 0.0F;
    snapshot.forwardDuty = myMotorForwardsPwm ? myMotorForwardsPwm->duty() : 0.0F;
    snapshot.backwardDuty = myMotorBackwardsPwm ? myMotorBackwardsPwm->duty() : 0.0F;
    myCommunication.publishTelemetry(nowMs, snapshot, myRuntimeControl);
}

void Logic::shutdownMqttOverlay() noexcept
{
    myRuntimeControl.forceDisarm(app::runtime::StateReason::OperatorStop);
    disableMqttOutput();
    myCommunication.disconnect();
}
} // namespace app::logic
