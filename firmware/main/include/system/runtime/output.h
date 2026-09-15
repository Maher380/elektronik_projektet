#pragma once

namespace driver::motor { class Interface; }
namespace driver::servo { class Interface; }
namespace driver::pwm { class Interface; }
namespace driver::gpio { class Interface; }

namespace app::runtime
{
/** Apply authorized output and disable nSLEEP on an actuator error.
 * PWM duty getters are driver state, not physical feedback. Disarmed output
 * explicitly disables the bridge and clears both PWM commands; an armed
 * obstacle/sensor stop retains SCRUM-16's active braking behavior.
 */
bool applyOutput(driver::motor::Interface& motor, driver::servo::Interface& servo,
                 driver::pwm::Interface& forward, driver::pwm::Interface& backward,
                 driver::gpio::Interface& sleep, float duty, float steeringDegrees,
                 bool armed) noexcept;
} // namespace app::runtime
