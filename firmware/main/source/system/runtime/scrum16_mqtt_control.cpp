#include "system/runtime/control.h"

namespace app::runtime
{
bool Control::authorizeScrum16Action(std::uint32_t nowMs, bool validEnvironment,
                                     float plannedDuty, bool brakeRequested) noexcept
{
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

    // These states describe the original decision; they never replace it.
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
