/**
 * @file pi_link.cpp
 * @brief Host tests for the Pi link: its lines, and how Control takes them. See ADR 0012.
 *
 * Two promises are tested here. A damaged line is dropped whole, never partly applied.
 * And the Pi never holds the lease: its lines drive an armed car but never arm one, never
 * keep one armed, and never lock the operator out of the car.
 */
#include "test/pi_link.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "system/communication/pi_link.h"
#include "system/runtime/control.h"

namespace
{
using app::pi_link::CarLine;
using app::pi_link::PiLine;
using app::pi_link::crc16;
using app::pi_link::formatCarLine;
using app::pi_link::parsePiLine;
using app::runtime::Command;
using app::runtime::CommandType;
using app::runtime::Control;
using app::runtime::ControlState;
using app::runtime::PiLink;
using app::runtime::PiState;

bool fail(const char* what) noexcept
{
    std::printf("Pi link test failed: %s\n", what);
    return false;
}

/** A P line with a correct CRC, built the way the Pi builds one. */
void piLineOf(const char* body, char* out, std::size_t size) noexcept
{
    std::snprintf(out, size, "%s*%04X", body, static_cast<unsigned>(crc16(body, std::strlen(body))));
}

Command commandOf(CommandType type, std::uint32_t requestId) noexcept
{
    Command command{};
    command.type = type;
    command.requestId = requestId;
    command.hasRequestId = true;
    std::strncpy(command.sessionId.data(), "page-session", command.sessionId.size() - 1U);
    return command;
}

Command driveOf(float steering, float speed) noexcept
{
    Command command{commandOf(CommandType::Drive, 0U)};
    command.hasRequestId = false;
    command.steeringCommand = steering;
    command.speedCommand = speed;
    return command;
}

/** A Ford armed by the operator page, as ADR 0005 and 0012 have it. */
Control armedFord(std::uint32_t nowMs) noexcept
{
    Control ford{false, true};
    ford.setMqttConnected(true);
    (void)ford.handleCommand(commandOf(CommandType::Start, 1U), nowMs);
    return ford;
}

bool theCrcIsCcittFalse() noexcept
{
    if (crc16("123456789", 9U) != 0x29B1U) { return fail("CRC-16/CCITT-FALSE check value"); }
    // The example lines in ADR 0012 must be ones a parser accepts.
    PiLine line{};
    if (!parsePiLine("P,1,1234,driving,-12.5,35.0*71D3", line)
        || (line.sequence != 1234U) || (line.state != PiState::Driving)
        || (line.steeringCommand != -12.5F) || (line.speedCommand != 35.0F))
    {
        return fail("ADR 0012's example P line should parse");
    }
    return true;
}

bool aDamagedLineIsDroppedWhole() noexcept
{
    char good[96]{};
    piLineOf("P,1,7,driving,10.0,20.0", good, sizeof(good));
    PiLine line{};
    if (!parsePiLine(good, line)) { return fail("a good line was dropped"); }

    char withCr[100]{};
    std::snprintf(withCr, sizeof(withCr), "%s\r", good);
    if (!parsePiLine(withCr, line)) { return fail("a trailing carriage return should be allowed"); }

    // One flipped character anywhere, the CRC included, and the line is gone.
    for (std::size_t index{0U}; good[index] != '\0'; ++index)
    {
        char damaged[96]{};
        std::memcpy(damaged, good, sizeof(good));
        damaged[index] = static_cast<char>(damaged[index] ^ 0x01);
        PiLine untouched{};
        untouched.sequence = 99U;
        if (parsePiLine(damaged, untouched)) { return fail("a damaged line was accepted"); }
        if (untouched.sequence != 99U) { return fail("a dropped line changed the output"); }
    }

    // Valid CRCs on bodies that are wrong in some other way.
    const char* const wrong[]{
        "P,2,7,driving,10.0,20.0",       // unknown version
        "C,1,7,driving,10.0,20.0",       // not a Pi line
        "P,1,7,racing,10.0,20.0",        // unknown state
        "P,1,7,driving,91.0,20.0",       // steering out of range
        "P,1,7,driving,10.0,-100.5",     // speed out of range
        "P,1,7,driving,nan,20.0",        // not a number
        "P,1,7,driving,10.0",            // a field missing
        "P,1,7,driving,10.0,20.0,1",     // a field too many
        "P,1,-7,driving,10.0,20.0",      // negative sequence
        "P,1,7,driving,10.0x,20.0",      // trailing junk in a number
        "P,1,7,driving,,20.0",           // empty number
    };
    for (const char* body : wrong)
    {
        char text[96]{};
        piLineOf(body, text, sizeof(text));
        if (parsePiLine(text, line))
        {
            std::printf("  accepted: %s\n", text);
            return fail("a line with a valid CRC but a wrong body was accepted");
        }
    }
    if (parsePiLine("P,1,7,driving,10.0,20.0", line)) { return fail("a line without a CRC was accepted"); }
    return true;
}

bool theCarLineCarriesACheckedCrc() noexcept
{
    CarLine car{};
    car.sequence = 5678U;
    car.controlState = "armed";
    car.reason = "none";
    car.driveStyle = "manual_by_remote";
    car.startModule = "none";
    car.piLink = "driving";
    car.appliedSteering = -12.5F;
    car.appliedSpeed = 35.0F;
    car.odometerDistanceM = 3.412F;
    car.odometerSpeedMs = 1.18F;
    car.odometerSpeedSource = "per_gap";
    car.lastPiSequence = 1234U;

    char out[200]{};
    const std::size_t written{formatCarLine(car, out, sizeof(out))};
    const char* body{"C,1,5678,armed,none,manual_by_remote,0,none,driving,-12.5,35.0,3.412,1.180,per_gap,1234"};
    char expected[200]{};
    std::snprintf(expected, sizeof(expected), "%s*%04X\n", body,
                  static_cast<unsigned>(crc16(body, std::strlen(body))));
    if ((written != std::strlen(expected)) || (std::strcmp(out, expected) != 0))
    {
        std::printf("  got:  %s  want: %s", out, expected);
        return fail("the car line is not what the Pi expects");
    }

    // Without an odometer, the measured fields are empty rather than a made-up number.
    car.odometerDistanceM = std::numeric_limits<float>::quiet_NaN();
    car.odometerSpeedMs = std::numeric_limits<float>::quiet_NaN();
    car.odometerSpeedSource = "";
    (void)formatCarLine(car, out, sizeof(out));
    if (std::strstr(out, ",35.0,,,,1234*") == nullptr) { return fail("missing odometer fields should be empty"); }

    // Too small a buffer writes nothing rather than half a line.
    char tiny[20]{};
    if ((formatCarLine(car, tiny, sizeof(tiny)) != 0U) || (tiny[0] != '\0'))
    {
        return fail("a line that does not fit should not be written at all");
    }
    return true;
}

/** The Pi drives an armed car, and nothing more. */
bool thePiDrivesButNeverArms() noexcept
{
    Control disarmed{false, true};
    disarmed.setMqttConnected(true);
    if (disarmed.handlePiLine(PiState::Driving, 20.0F, 50.0F, 0U)) { return fail("a disarmed car took the Pi's drive"); }
    if (disarmed.controlState() != ControlState::Disarmed) { return fail("a Pi line armed the car"); }
    if (disarmed.remoteDrive(10U, 500U).speedCommand != 0.0F) { return fail("a disarmed car drives"); }

    Control ford{armedFord(0U)};
    if (!ford.handlePiLine(PiState::Driving, 20.0F, 50.0F, 100U)) { return fail("an armed car refused the Pi's drive"); }
    const auto drive = ford.remoteDrive(110U, 500U);
    if ((drive.steeringCommand != 20.0F) || (drive.speedCommand != 50.0F))
    {
        return fail("the car should follow a Driving line");
    }
    if (ford.piLink(110U) != PiLink::Driving) { return fail("the link should read driving"); }

    // Not a heartbeat: with the page gone, the Pi alone cannot keep the car armed.
    for (std::uint32_t now{150U}; now < 3200U; now += 50U)
    {
        (void)ford.handlePiLine(PiState::Driving, 20.0F, 50.0F, now);
        (void)ford.remoteDrive(now, 500U);
    }
    if (ford.controlState() != ControlState::Disarmed) { return fail("the Pi's lines kept the lease alive"); }
    return true;
}

bool waitingOrLostAsksForNoDriveAtOnce() noexcept
{
    for (const auto state : {PiState::Waiting, PiState::Lost})
    {
        Control ford{armedFord(0U)};
        (void)ford.handlePiLine(PiState::Driving, -30.0F, 40.0F, 100U);
        // The commands of a waiting or lost line are ignored, whatever they say.
        if (ford.handlePiLine(state, 80.0F, 90.0F, 150U)) { return fail("a non-driving line was taken"); }
        const auto drive = ford.remoteDrive(160U, 500U);
        if ((drive.speedCommand != 0.0F) || (drive.steeringCommand != -30.0F))
        {
            return fail("waiting or lost should stop the motor and hold the steering");
        }
        if (ford.controlState() != ControlState::Armed) { return fail("the Pi's state disarmed the car"); }
    }
    return true;
}

bool aQuietPiIsGoneAfterHalfASecond() noexcept
{
    Control ford{armedFord(0U)};
    if (ford.piLink(0U) != PiLink::Gone) { return fail("a Pi never heard from should be gone"); }
    (void)ford.handlePiLine(PiState::Driving, 10.0F, 30.0F, 100U);
    if (ford.piLink(599U) != PiLink::Driving) { return fail("a fresh Pi should not be gone"); }
    if (ford.piLink(600U) != PiLink::Gone) { return fail("a Pi quiet for 500 ms should be gone"); }

    // The drive timeout stops the motor, keeps the steering, and keeps the car armed; the
    // page's heartbeat holds the lease meanwhile.
    (void)ford.handleCommand(commandOf(CommandType::Heartbeat, 0U), 590U);
    const auto drive = ford.remoteDrive(600U, 500U);
    if ((drive.speedCommand != 0.0F) || (drive.steeringCommand != 10.0F))
    {
        return fail("a gone Pi should leave no drive and the steering held");
    }
    if (ford.controlState() != ControlState::Armed) { return fail("a gone Pi disarmed the car"); }
    return true;
}

/** While the Pi drives, the page's sliders hold the lease but do not steer. */
bool thePageHoldsTheLeaseWhileThePiDrives() noexcept
{
    Control ford{armedFord(0U)};
    std::uint32_t now{0U};
    for (; now < 5000U; now += 50U)
    {
        (void)ford.handlePiLine(PiState::Driving, 15.0F, 25.0F, now);
        if ((now % 100U) == 0U)
        {
            // The page streams its sliders whatever they say; they must not reach the car.
            if (!ford.handleCommand(driveOf(-60.0F, 90.0F), now).accepted)
            {
                return fail("the lease holder's drive should be accepted as a heartbeat");
            }
        }
        const auto drive = ford.remoteDrive(now, 500U);
        if ((drive.steeringCommand != 15.0F) || (drive.speedCommand != 25.0F))
        {
            return fail("the page's sliders reached the car while the Pi drove");
        }
    }
    if (ford.controlState() != ControlState::Armed) { return fail("the page's drives should hold the lease"); }

    // Once the Pi stops driving, the page's sliders drive again.
    (void)ford.handlePiLine(PiState::Waiting, 0.0F, 0.0F, now);
    (void)ford.handleCommand(driveOf(-60.0F, 90.0F), now + 10U);
    const auto drive = ford.remoteDrive(now + 20U, 500U);
    if ((drive.steeringCommand != -60.0F) || (drive.speedCommand != 90.0F))
    {
        return fail("the page should drive once the Pi is no longer driving");
    }

    // PANIC STOP still disarms a car the Pi is driving.
    (void)ford.handlePiLine(PiState::Driving, 15.0F, 25.0F, now + 30U);
    (void)ford.handleCommand(commandOf(CommandType::Stop, 2U), now + 40U);
    if (ford.controlState() != ControlState::Disarmed) { return fail("stop should disarm a Pi-driven car"); }
    if (ford.remoteDrive(now + 50U, 500U).speedCommand != 0.0F) { return fail("a stopped car still drives"); }
    return true;
}

/**
 * @brief While the Pi waits, the operator's sliders drive, and its lines do not cancel them.
 *
 * Found on the car, 2026-10-07: the page's slider drove the wheel for one telemetry sample
 * and stopped, because every waiting line zeroed the speed, whoever had set it.
 */
bool aWaitingPiLeavesThePageDriving() noexcept
{
    Control ford{armedFord(0U)};
    for (std::uint32_t now{0U}; now < 2000U; now += 50U)
    {
        (void)ford.handlePiLine(PiState::Waiting, 0.0F, 0.0F, now);
        if ((now % 100U) == 0U) { (void)ford.handleCommand(driveOf(-20.0F, 13.0F), now + 1U); }
        const auto drive = ford.remoteDrive(now + 2U, 500U);
        if ((drive.steeringCommand != -20.0F) || (drive.speedCommand != 13.0F))
        {
            return fail("a waiting Pi cancelled the page's drive");
        }
    }

    // But a drive the Pi asked for still stops the moment the Pi says it is lost.
    (void)ford.handlePiLine(PiState::Driving, 10.0F, 30.0F, 2100U);
    (void)ford.handlePiLine(PiState::Lost, 10.0F, 30.0F, 2150U);
    if (ford.remoteDrive(2160U, 500U).speedCommand != 0.0F) { return fail("a lost Pi's drive went on"); }
    return true;
}

bool otherStylesIgnoreThePi() noexcept
{
    Control ford{false, true};
    ford.setMqttConnected(true);
    if (!ford.setDriveStyle(app::navigation::DriveStyle::SpeedCalibration)) { return fail("select SpeedCalibration"); }
    (void)ford.handleCommand(commandOf(CommandType::Start, 1U), 0U);
    if (ford.handlePiLine(PiState::Driving, 20.0F, 50.0F, 10U))
    {
        return fail("a calibration style took the Pi's drive");
    }
    return true;
}
} // namespace

namespace test
{
bool runPiLinkTest() noexcept
{
    const bool ok = theCrcIsCcittFalse()
        && aDamagedLineIsDroppedWhole()
        && theCarLineCarriesACheckedCrc()
        && thePiDrivesButNeverArms()
        && waitingOrLostAsksForNoDriveAtOnce()
        && aQuietPiIsGoneAfterHalfASecond()
        && thePageHoldsTheLeaseWhileThePiDrives()
        && aWaitingPiLeavesThePageDriving()
        && otherStylesIgnoreThePi();
    if (ok) { std::printf("Pi link test succeeded!\n"); }
    return ok;
}
} // namespace test
