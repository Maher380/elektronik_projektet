#include <cmath>
#include <cstdio>

#include "driver/pwm/stub.h"
#include "driver/servo/ford.h"
#include "test/servo.h"

namespace
{
bool expect(bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("Servo test failed: %s\n", message);
        return false;
    }

    return true;
}

// Pulse width in microseconds that the PWM sends, from its duty and frequency.
float pulseUs(const driver::pwm::Interface& pwm) noexcept
{
    return pwm.duty() * 1'000'000.0F / static_cast<float>(pwm.frequencyHz());
}

bool isPulse(const driver::pwm::Interface& pwm, float expectedUs) noexcept
{
    return std::fabs(pulseUs(pwm) - expectedUs) < 0.5F;
}
} // namespace

namespace test
{
bool runServoTest() noexcept
{
    driver::pwm::Stub pwm{driver::pwm::Config{.pin = 9U, .frequencyHz = 50U}};
    driver::servo::Ford servo{pwm};

    bool passed = expect(!servo.setDirection(0.0F), "uninitialized servo should reject a direction")
        && expect(servo.init(), "init should succeed")
        && expect(pwm.isInitialized(), "init should initialize the PWM")
        && expect(isPulse(pwm, 1500.0F), "init should center the servo at 1500 us");

    passed = passed
        && expect(servo.setDirection(-90.0F) && isPulse(pwm, 1000.0F), "full left should be 1000 us")
        && expect(servo.setDirection(90.0F) && isPulse(pwm, 2000.0F), "full right should be 2000 us")
        && expect(servo.setDirection(-45.0F) && isPulse(pwm, 1250.0F), "half left should be 1250 us")
        && expect(servo.setDirection(45.0F) && isPulse(pwm, 1750.0F), "half right should be 1750 us")
        && expect(servo.getDirection() == 45.0F, "direction should be stored");

    // Commands past full lock are clamped, not rejected.
    passed = passed
        && expect(servo.setDirection(120.0F) && isPulse(pwm, 2000.0F), "past full right should clamp to 2000 us")
        && expect(servo.getDirection() == 90.0F, "clamped direction should be stored")
        && expect(servo.setDirection(-120.0F) && isPulse(pwm, 1000.0F), "past full left should clamp to 1000 us");

    passed = passed
        && expect(servo.center() && isPulse(pwm, 1500.0F), "center should be 1500 us")
        && expect(servo.deinit() && !servo.isInitialized() && !pwm.isInitialized(),
                  "deinit should stop the servo and its PWM");

    return passed;
}
} // namespace test
