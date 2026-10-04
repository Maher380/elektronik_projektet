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
enum class DriveStyle : std::uint8_t
{ DecideAction, SlowLeft, SlowRight, GradualSweep, ManualByRemote, GapCalibration };

} // namespace app::navigation
