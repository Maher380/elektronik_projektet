#include <cmath>
#include <cstdio>

#include "driver/pwm/stub.h"
#include "driver/servo/ford.h"
#include "driver/servo/mg90s.h"
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

bool runFordTest() noexcept
{
    driver::pwm::Stub pwm{driver::pwm::Config{.pin = 9U, .frequencyHz = 50U}};
    driver::servo::Ford servo{pwm};

    bool passed = expect(!servo.setDirection(0.0F), "uninitialized servo should reject a direction")
        && expect(servo.init(), "init should succeed")
        && expect(pwm.isInitialized(), "init should initialize the PWM")
        && expect(isPulse(pwm, 1582.0F), "init should center the servo at 1582 us");

    passed = passed
        && expect(servo.setDirection(-90.0F) && isPulse(pwm, 1182.0F), "full left should be 1182 us")
        && expect(servo.setDirection(90.0F) && isPulse(pwm, 1982.0F), "full right should be 1982 us")
        && expect(servo.setDirection(-45.0F) && isPulse(pwm, 1382.0F), "half left should be 1382 us")
        && expect(servo.setDirection(45.0F) && isPulse(pwm, 1782.0F), "half right should be 1782 us")
        && expect(servo.getDirection() == 45.0F, "direction should be stored");

    // Commands past full lock are clamped, not rejected.
    passed = passed
        && expect(servo.setDirection(120.0F) && isPulse(pwm, 1982.0F), "past full right should clamp to 1982 us")
        && expect(servo.getDirection() == 90.0F, "clamped direction should be stored")
        && expect(servo.setDirection(-120.0F) && isPulse(pwm, 1182.0F), "past full left should clamp to 1182 us");

    passed = passed
        && expect(servo.center() && isPulse(pwm, 1582.0F), "center should be 1582 us")
        && expect(servo.deinit() && !servo.isInitialized() && !pwm.isInitialized(),
                  "deinit should stop the servo and its PWM");

    return passed;
}

bool runMg90sTest() noexcept
{
    driver::pwm::Stub fastPwm{driver::pwm::Config{.pin = 9U, .frequencyHz = 330U}};
    driver::servo::Mg90s fastServo{fastPwm};
    bool passed = expect(!fastServo.init() && !fastPwm.isInitialized(), "MG90S should refuse a 330 Hz PWM");

    driver::pwm::Stub pwm{driver::pwm::Config{.pin = 9U, .frequencyHz = 50U}};
    driver::servo::Mg90s servo{pwm};

    passed = passed
        && expect(!servo.setDirection(0.0F), "uninitialized MG90S should reject a direction")
        && expect(servo.init(), "MG90S init should succeed")
        && expect(pwm.isInitialized(), "MG90S init should initialize the PWM")
        && expect(isPulse(pwm, 1931.0F), "MG90S init should center at 1931 us");

    passed = passed
        && expect(servo.setDirection(-90.0F) && isPulse(pwm, 1631.0F), "MG90S full left should be 1631 us")
        && expect(servo.setDirection(90.0F) && isPulse(pwm, 2250.0F), "MG90S full right should be 2250 us")
        && expect(servo.setDirection(-45.0F) && isPulse(pwm, 1781.0F), "MG90S half left should be 1781 us")
        && expect(servo.setDirection(45.0F) && isPulse(pwm, 2090.5F), "MG90S half right should be 2090.5 us")
        && expect(servo.setDirection(120.0F) && isPulse(pwm, 2250.0F), "MG90S past full right should clamp")
        && expect(servo.getDirection() == 90.0F, "MG90S clamped direction should be stored")
        && expect(servo.deinit() && !pwm.isInitialized(), "MG90S deinit should stop its PWM");

    return passed;
}
} // namespace

namespace test
{
bool runServoTest() noexcept
{
    const bool fordPassed = runFordTest();
    const bool mg90sPassed = runMg90sTest();
    return fordPassed && mg90sPassed;
}
} // namespace test
