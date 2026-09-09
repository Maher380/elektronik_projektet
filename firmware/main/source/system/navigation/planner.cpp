#include "system/navigation/planner.h"
#include <algorithm>
#include <cmath>

namespace app::navigation
{
void Planner::setDriverStyle(DriverStyle style) noexcept
{
    myStyle = style;
    if (style == DriverStyle::GradualSweep)
    {
        mySweepDegrees = -90.0F;
        mySweepDirection = 1.0F;
    }
}

Decision Planner::decide(const Distances& distances) noexcept
{
    if (!std::all_of(distances.begin(), distances.end(),
                    [](float value) { return std::isfinite(value) && value > 0.0F; }))
    {
        return {};
    }
    if (myStyle == DriverStyle::DecideAction)
    {
        if (distances[Forward] > std::max(distances[Left], distances[Right]))
        {
            return {0.0F, 0.5F, Forward};
        }
        if (distances[Left] > distances[Right]) { return {-90.0F, 0.5F, Left}; }
        return {90.0F, 0.5F, Right};
    }

    // The SCRUM-16 test styles stop for an obstacle at ANY sensor.
    const auto closest = static_cast<std::size_t>(
        std::min_element(distances.begin(), distances.end()) - distances.begin());
    if (myStyle == DriverStyle::SlowLeft) { return {-90.0F, 0.2F, closest}; }
    if (myStyle == DriverStyle::SlowRight) { return {90.0F, 0.2F, closest}; }
    if (myStyle != DriverStyle::GradualSweep) { return {}; }
    const Decision decision{mySweepDegrees, 0.2F, closest};
    if (mySweepDegrees >= 90.0F) { mySweepDirection = -1.0F; }
    else if (mySweepDegrees <= -90.0F) { mySweepDirection = 1.0F; }
    mySweepDegrees += mySweepDirection * 5.0F;
    return decision;
}
} // namespace app::navigation
