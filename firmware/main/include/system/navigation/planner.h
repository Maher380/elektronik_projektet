#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace app::navigation
{
/** Sensor order shared by wiring, navigation and MQTT telemetry. */
enum Position : std::size_t { Left, Forward, Right, SensorCount };
using Distances = std::array<float, SensorCount>;
enum class DriverStyle : std::uint8_t { DecideAction, SlowLeft, SlowRight, GradualSweep };

/** Navigation intent; runtime authorization and safety are applied separately. */
struct Decision
{
    float steeringDegrees{0.0F};
    float driveDuty{0.0F};
    std::size_t pathSensor{Forward};
};

/** Host-testable SCRUM-16 route selection, including its existing tie behavior. */
class Planner final
{
public:
    void setDriverStyle(DriverStyle style) noexcept;
    DriverStyle driverStyle() const noexcept { return myStyle; }
    Decision decide(const Distances& distances) noexcept;
private:
    DriverStyle myStyle{DriverStyle::DecideAction};
    float mySweepDegrees{-90.0F};
    float mySweepDirection{1.0F};
};
} // namespace app::navigation
