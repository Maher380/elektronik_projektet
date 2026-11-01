#pragma once

#include <cstdint>
#include "driver/motor/interface.h"

namespace app::logic
{
class MotorKickstart
{
public:
    void arm() noexcept { myAvailable = true; myKicking = false; }

    float apply(std::uint32_t nowMs, float requestedDuty,
                driver::motor::Direction direction, bool automatic) noexcept
    {
        if (!automatic || requestedDuty <= 0.0F)
        {
            myKicking = false;
            return requestedDuty;
        }
        if (direction != driver::motor::Direction::Forward)
        { myKicking = false; return requestedDuty; }
        if (myAvailable)
        {
            myStartedMs = nowMs;
            myAvailable = false;
            myKicking = true;
        }
        if (myKicking && nowMs - myStartedMs < DurationMs) { return 1.0F; }
        myKicking = false;
        return requestedDuty;
    }

private:
    static constexpr std::uint32_t DurationMs{100U};
    std::uint32_t myStartedMs{0U};
    bool myAvailable{true};
    bool myKicking{false};
};
} // namespace app::logic
