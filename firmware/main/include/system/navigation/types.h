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
enum class DriverStyle : std::uint8_t { DecideAction, SlowLeft, SlowRight, GradualSweep, ManualByRemote };

} // namespace app::navigation
