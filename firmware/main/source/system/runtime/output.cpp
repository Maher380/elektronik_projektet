#include "system/runtime/output.h"
#include "driver/motor/interface.h"
#include "driver/servo/interface.h"
#include "driver/pwm/interface.h"
#include "driver/gpio/interface.h"
#include <cmath>

namespace app::runtime
{
bool applyOutput(driver::motor::Interface& motor, driver::servo::Interface& servo,
                 driver::pwm::Interface& forward, driver::pwm::Interface& backward,
                 driver::gpio::Interface& sleep, float duty, float angle, bool armed) noexcept
{
    const auto disable = [&]() noexcept {
        sleep.write(false);
        // Do not short-circuit: try BOTH outputs even when one driver fails.
        const bool a = forward.setDuty(0.0F);
        const bool b = backward.setDuty(0.0F);
        return a && b;
    };
    if (!armed) { return disable(); }
    if (!motor.isInitialized() || !servo.isInitialized() || !sleep.isInitialized()
        || !std::isfinite(duty) || duty < 0.0F || duty > 0.5F
        || !servo.setDirection(angle))
    {
        disable();
        return false;
    }
    const bool applied = duty > 0.0F
        ? motor.setDirection(driver::motor::Direction::Forward)
            && motor.setSpeed(duty, driver::motor::StopMode::Coast)
        : motor.stop(driver::motor::StopMode::Brake);
    // MP6550 currently hides PWM failures. Check the resulting driver state
    // here without changing the teammate-owned motor implementation.
    const float expectedForward = duty > 0.0F ? duty : 1.0F;
    const float expectedBackward = duty > 0.0F ? 0.0F : 1.0F;
    if (!applied || forward.duty() != expectedForward || backward.duty() != expectedBackward)
    {
        disable();
        return false;
    }
    sleep.write(true);
    return true;
}
} // namespace app::runtime
