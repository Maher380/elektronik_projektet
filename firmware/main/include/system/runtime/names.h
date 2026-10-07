/**
 * @file names.h
 * @brief The wire names of the runtime states, shared by MQTT telemetry and the Pi link.
 *
 * One table, so that the operator page and the Pi read the same word for the same state.
 */

#pragma once

#include "system/navigation/types.h"
#include "system/runtime/control.h"

namespace app::runtime
{

inline const char* toString(const ControlState state) noexcept
{
    return state == ControlState::Armed ? "armed" : "disarmed";
}

inline const char* toString(const StateReason reason) noexcept
{
    switch (reason)
    {
        case StateReason::ActuatorFault: return "actuator_fault";
        case StateReason::Boot: return "boot";
        case StateReason::OperatorStop: return "operator_stop";
        case StateReason::Obstacle: return "obstacle";
        case StateReason::SensorFault: return "sensor_fault";
        case StateReason::HeartbeatTimeout: return "heartbeat_timeout";
        case StateReason::MqttDisconnected: return "mqtt_disconnected";
        case StateReason::MessageOverflow: return "message_overflow";
        case StateReason::DriveTimeout: return "drive_timeout";
        case StateReason::DriveStyleFinished: return "drive_style_finished";
        case StateReason::Overheated: return "overheated";
        case StateReason::None: return "none";
    }
    return "none";
}

inline const char* toString(const PiLink link) noexcept
{
    switch (link)
    {
        case PiLink::Gone: return "gone";
        case PiLink::Waiting: return "waiting";
        case PiLink::Driving: return "driving";
        case PiLink::Lost: return "lost";
    }
    return "gone";
}

} // namespace app::runtime

namespace app::navigation
{

inline const char* toString(const DriveStyle style) noexcept
{
    switch (style)
    {
        case DriveStyle::DecideAction: return "decide_action";
        case DriveStyle::SlowLeft: return "slow_left";
        case DriveStyle::SlowRight: return "slow_right";
        case DriveStyle::GradualSweep: return "gradual_sweep";
        case DriveStyle::ManualByRemote: return "manual_by_remote";
        case DriveStyle::GapCalibration: return "gap_calibration";
        case DriveStyle::SpeedCalibration: return "speed_calibration";
        case DriveStyle::Disabled: return "disabled";
    }
    return "invalid";
}

} // namespace app::navigation
