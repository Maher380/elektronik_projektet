#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace app::navigation
{
/** Sensor order shared by wiring, navigation and MQTT telemetry. */
enum Position : std::size_t { Left, Forward, Right, SensorCount };
using Distances = std::array<float, SensorCount>;
/** ManualByRemote: an operator drives live from the web page (Ford only). */
/** GapCalibration: the car drives a fixed script to measure its own magnet gaps (Ford only). */
/** SpeedCalibration: the car drives a fixed script on the floor to measure speed per duty (Ford only). */
/**
 * Disabled: safe mode parked the car because a temperature got too high (Ford only). It
 * never drives; the operator selects another style to leave it. The car selects it itself,
 * so it is not offered over MQTT.
 */
enum class DriveStyle : std::uint8_t
{ DecideAction, SlowLeft, SlowRight, GradualSweep, ManualByRemote, GapCalibration, SpeedCalibration, Disabled };

} // namespace app::navigation
