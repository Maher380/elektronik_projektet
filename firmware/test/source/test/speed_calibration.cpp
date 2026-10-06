/**
 * @file speed_calibration.cpp
 * @brief Host tests for the speed calibration state machine.
 *
 * A simulated car turns the duty the run asks for into speed and wheel pulses on the
 * odometer stub, ten milliseconds at a time, the same period as Ford's loop. It is shaped
 * like the first floor run: it will not start below duty 0.11, stalls below 0.065, lags
 * its duty by 0.4 s and brakes at 4 m/s^2. That is enough to check the sequencing, that
 * the speed loop holds its targets, that the legs stop on the mark once the stopping
 * distance has been learned, and that the lowest-speed legs find the stall.
 */
#include "test/speed_calibration.h"

#include <cmath>
#include <cstdio>
#include <limits>

#include "driver/odometer/stub.h"
#include "system/ford.h"
#include "system/logic/speedCalibration.h"

namespace
{
namespace ford = app::ford;
namespace spd = app::ford::speed_calibration;
using app::logic::SpeedCalibration;
using Outcome = SpeedCalibration::Outcome;
using driver::odometer::Stub;

constexpr float NoSensor{std::numeric_limits<float>::quiet_NaN()};
constexpr std::uint32_t TickMs{10U};
constexpr float Circumference{static_cast<float>(ford::WheelCircumferenceM)};
constexpr double MetersPerPulse{ford::WheelCircumferenceM / ford::OdometerMagnets};

bool fail(const char* what) noexcept
{
    std::printf("Speed calibration test failed: %s\n", what);
    return false;
}

Stub makeOdometer() noexcept
{
    return Stub{driver::odometer::Config{
        .pulsesPerRevolution = ford::OdometerMagnets,
        .wheelDiameterM = static_cast<float>(ford::WheelDiameterM),
    }};
}

/** Steady speed of the simulated car at a duty while it is rolling. */
float steadySpeed(const float duty) noexcept { return 13.0F * (duty - 0.06F); }

/** The duty the simulated car needs to hold a speed. */
float dutyFor(const float speedMs) noexcept { return (speedMs / 13.0F) + 0.06F; }

/** The simulated car. */
struct Car
{
    Stub odometer{makeOdometer()};
    double travelledM{0.0};
    std::uint32_t pulses{0U};
    float speedMs{0.0F};
    bool rolling{false};
    /** Duty below which it will not start; raise it to make a car that never starts. */
    float startDuty{0.11F};
};

/** One loop tick: ask the run for a duty, then move the car for TickMs. */
float tick(SpeedCalibration& run, Car& car, std::uint32_t& nowMs,
           bool armed = true, float temperatureC = NoSensor) noexcept
{
    const float signedDuty = run.update(nowMs, armed, temperatureC, &car.odometer);
    const float duty = std::fabs(signedDuty);
    constexpr float dt{static_cast<float>(TickMs) / 1000.0F};

    if (duty == 0.0F) { car.rolling = false; }
    else if (!car.rolling && (duty >= car.startDuty)) { car.rolling = true; }
    else if (car.rolling && (duty < 0.065F)) { car.rolling = false; }

    if (car.rolling) { car.speedMs += (steadySpeed(duty) - car.speedMs) * dt / 0.4F; }
    else { car.speedMs = std::fmax(0.0F, car.speedMs - (4.0F * dt)); }

    car.travelledM += static_cast<double>(car.speedMs) * dt;
    const auto total = static_cast<std::uint32_t>(car.travelledM / MetersPerPulse);
    if (total > car.pulses)
    {
        car.odometer.simulatePulses(total - car.pulses);
        car.pulses = total;
    }
    car.odometer.simulateSpeed(car.speedMs);
    nowMs += TickMs;
    return signedDuty;
}

/** Run to the end, collecting each finished leg. Fails if it does not end in time. */
bool runToEnd(SpeedCalibration& run, Car& car, std::uint32_t& nowMs, SpeedCalibration::Leg* legs) noexcept
{
    bool seen[spd::LegCount]{};
    for (std::uint32_t ticks{0U}; run.isRunning(); ++ticks)
    {
        if (ticks > 100000U) { return false; }
        const float duty = tick(run, car, nowMs);

        // Braking asks for no drive, and the sign of a drive is the leg's direction.
        if (run.isBraking() && (duty != 0.0F)) { return false; }
        if (run.phase() == SpeedCalibration::Phase::Driving && (duty != 0.0F)
            && ((duty > 0.0F) != SpeedCalibration::planOf(run.legIndex()).forward))
        {
            return false;
        }

        if (run.hasLastLeg())
        {
            const auto& leg = run.lastLeg();
            if ((leg.index < spd::LegCount) && !seen[leg.index])
            {
                seen[leg.index] = true;
                legs[leg.index] = leg;
            }
        }
    }
    for (const bool each : seen) { if (!each) { return false; } }
    return true;
}

bool startRun(SpeedCalibration& run, Car& car, std::uint32_t nowMs) noexcept
{
    if (!car.odometer.init()) { return false; }
    run.start(nowMs, ford::OdometerMagnets, Circumference);
    return run.phase() == SpeedCalibration::Phase::Driving;
}

/** The legs follow the recipe: targets out and back, a step each way, then the lowest speed. */
bool theRecipeAlternatesDirection() noexcept
{
    for (std::uint8_t leg{0U}; leg < spd::LegCount; ++leg)
    {
        const auto plan = SpeedCalibration::planOf(leg);
        if (plan.forward != ((leg % 2U) == 0U)) { return fail("legs should alternate forward and back"); }
    }
    const auto up = SpeedCalibration::planOf(spd::TargetCount * 2U);
    const auto down = SpeedCalibration::planOf((spd::TargetCount * 2U) + 1U);
    if ((up.fromMs != spd::StepLowMs) || (up.targetMs != spd::StepHighMs)
        || (down.fromMs != spd::StepHighMs) || (down.targetMs != spd::StepLowMs))
    {
        return fail("the step legs should go low to high, then high to low");
    }
    if (!SpeedCalibration::planOf(spd::LegCount - 1U).minimum) { return fail("the run should end with the lowest speed"); }
    return true;
}

/** A whole run holds every target, stops on the mark once it has learned to, and finds the stall. */
bool aCleanRunLearnsTheCar() noexcept
{
    Car car{};
    SpeedCalibration run{};
    std::uint32_t nowMs{1000U};
    if (!startRun(run, car, nowMs)) { return fail("a started run should drive"); }

    SpeedCalibration::Leg legs[spd::LegCount]{};
    if (!runToEnd(run, car, nowMs, legs)) { return fail("the run did not drive every leg"); }
    if (run.phase() != SpeedCalibration::Phase::Finished) { return fail("a clean run should finish"); }

    for (std::uint8_t index{0U}; index < spd::LegCount; ++index)
    {
        const auto& leg = legs[index];
        if (leg.plan.minimum) { continue; }
        if (leg.outcome != Outcome::Reached)
        {
            std::printf("  leg %u: outcome %u\n", index, static_cast<unsigned>(leg.outcome));
            return fail("every target should be reached and held");
        }
        const float target = leg.plan.targetMs;
        if (std::fabs(leg.speedMs - target) > (0.04F * target))
        {
            std::printf("  leg %u: %.3f m/s for %.2f\n", index, static_cast<double>(leg.speedMs),
                        static_cast<double>(target));
            return fail("a held speed is more than 4 % off its target");
        }
        if (std::fabs(leg.duty - dutyFor(target)) > 0.01F) { return fail("the steady duty does not fit the car"); }
        if (!(leg.riseS > 0.0F)) { return fail("a reached target should have a rise time"); }
    }

    // The first legs brake on the starting guess for k; once it has been learned the car
    // should stop within a few pulses of the mark. The lowest-speed legs end where the
    // wheel stalls instead, about as far out as back, so they are left out.
    for (std::uint8_t index{4U}; index < spd::LegCount; ++index)
    {
        if (legs[index].plan.minimum) { continue; }
        if (std::fabs(legs[index].distanceM - spd::LegM) > 0.15F)
        {
            std::printf("  leg %u: %.3f m\n", index, static_cast<double>(legs[index].distanceM));
            return fail("a leg did not stop near the mark after learning");
        }
    }
    if ((run.stopK() < 0.08F) || (run.stopK() > 0.2F)) { return fail("k should approach v^2 / 8 = 0.125"); }

    // The simulated car rolls down to 0.07 and stalls at 0.06.
    for (std::uint8_t index{spd::LegCount - 2U}; index < spd::LegCount; ++index)
    {
        const auto& leg = legs[index];
        if (leg.outcome != Outcome::Lowest) { return fail("the lowest-speed leg should find the stall"); }
        if (std::fabs(leg.duty - 0.07F) > 0.001F) { return fail("the lowest rolling duty should be 0.07"); }
        if (!(leg.speedMs > 0.0F)) { return fail("the lowest rolling duty should have a speed"); }
    }

    // What was learned is kept for the next run.
    if (std::fabs(run.feedForward(1.0F, true) - dutyFor(1.0F)) > 0.01F)
    {
        return fail("the learned feed-forward should fit the car");
    }
    return true;
}

/** A car that never starts gives a NoStart on every leg, and the run still ends. */
bool aCarThatNeverStartsIsRecorded() noexcept
{
    Car car{};
    car.startDuty = 1.0F;
    SpeedCalibration run{};
    std::uint32_t nowMs{1000U};
    if (!startRun(run, car, nowMs)) { return fail("a started run should drive"); }

    SpeedCalibration::Leg legs[spd::LegCount]{};
    if (!runToEnd(run, car, nowMs, legs)) { return fail("the run did not end"); }
    for (const auto& leg : legs)
    {
        if (leg.outcome != Outcome::NoStart) { return fail("a wheel that never turned should be no_start"); }
    }
    return true;
}

/** Disarming, no odometer and an overheated motor each end the run with no drive. */
bool theRunEndsEarlyForTheRightReasons() noexcept
{
    {
        Car car{};
        SpeedCalibration run{};
        std::uint32_t nowMs{1000U};
        (void)startRun(run, car, nowMs);
        for (int i{0}; i < 50; ++i) { (void)tick(run, car, nowMs); }
        if (tick(run, car, nowMs, false) != 0.0F) { return fail("a disarmed run asked for drive"); }
        if (run.failure() != SpeedCalibration::Failure::Stopped) { return fail("a disarm should end the run as stopped"); }
    }
    {
        Car car{};
        SpeedCalibration run{};
        std::uint32_t nowMs{1000U};
        (void)startRun(run, car, nowMs);
        if (tick(run, car, nowMs, true, ford::MaxMotorTempC + 1.0F) != 0.0F)
        {
            return fail("an overheated run asked for drive");
        }
        if (run.failure() != SpeedCalibration::Failure::TooHot) { return fail("heat should end the run"); }
    }
    {
        SpeedCalibration run{};
        run.start(1000U, ford::OdometerMagnets, Circumference);
        if (run.update(1010U, true, NoSensor, nullptr) != 0.0F) { return fail("a run without an odometer asked for drive"); }
        if (run.failure() != SpeedCalibration::Failure::NoOdometer) { return fail("no odometer should end the run"); }
    }
    {
        SpeedCalibration run{};
        run.start(1000U, 0U, Circumference);
        if (run.isRunning()) { return fail("a wheel without magnets cannot be measured"); }
    }
    return true;
}
} // namespace

namespace test
{
bool runSpeedCalibrationTest() noexcept
{
    if (!theRecipeAlternatesDirection()) { return false; }
    if (!aCleanRunLearnsTheCar()) { return false; }
    if (!aCarThatNeverStartsIsRecorded()) { return false; }
    if (!theRunEndsEarlyForTheRightReasons()) { return false; }
    std::printf("Speed calibration test succeeded!\n");
    return true;
}
} // namespace test
