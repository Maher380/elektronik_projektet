/**
 * @file drive_style.cpp
 * @brief Host tests for drive style selection and the car/style split in Control.
 *
 * These cover the distinction ADR 0009 draws: which drive styles a Car supports is a
 * property of the car and fixed for its life, while whether operator Drive commands are
 * accepted follows the drive style selected right now. The two coincided while Ford had
 * one style, and `myManualByRemote` decided both.
 */
#include "test/drive_style.h"

#include <cstdio>
#include <cstring>

#include "system/runtime/control.h"

namespace
{
using app::navigation::DriveStyle;
using app::runtime::Command;
using app::runtime::CommandError;
using app::runtime::CommandType;
using app::runtime::Control;
using app::runtime::ControlState;

bool fail(const char* what) noexcept
{
    std::printf("Drive style test failed: %s\n", what);
    return false;
}

/** A command carrying a session id, which every command needs to be considered. */
Command commandOf(CommandType type, std::uint32_t requestId) noexcept
{
    Command command{};
    command.type = type;
    command.requestId = requestId;
    command.hasRequestId = true;
    std::strncpy(command.sessionId.data(), "test-session", command.sessionId.size() - 1U);
    return command;
}

/**
 * @brief Arm a car the way the operator console does: connect, then Start.
 *
 * The request id must advance between Starts, as the console's does: Control rejects a
 * repeat as a QoS-1 retransmission rather than re-arming on it.
 */
bool arm(Control& control, std::uint32_t nowMs, std::uint32_t requestId = 1U) noexcept
{
    control.setMqttConnected(true);
    return control.handleCommand(commandOf(CommandType::Start, requestId), nowMs).accepted;
}

/** A remote-driven car supports its own two styles, and boots on the operator-driven one. */
bool remoteDrivenCarSupportsOnlyItsOwnStyles() noexcept
{
    Control ford{false, true};

    // Ford boots to ManualByRemote and does not persist its style, so a calibration
    // selection cannot survive a battery swap and spin the wheel on the next Start.
    if (ford.configuration().driveStyle != DriveStyle::ManualByRemote)
    {
        return fail("a remote-driven car should start on ManualByRemote");
    }
    if (!ford.isRemoteDrivenCar()) { return fail("Ford should report itself remote-driven"); }
    if (!ford.isManualByRemoteStyle()) { return fail("Ford should start on the ManualByRemote style"); }

    // The autonomous styles need obstacle distances, which this car does not have.
    for (const auto style : {DriveStyle::DecideAction, DriveStyle::SlowLeft,
                             DriveStyle::SlowRight, DriveStyle::GradualSweep})
    {
        if (ford.setDriveStyle(style)) { return fail("a remote-driven car accepted an autonomous style"); }
    }

    // The measurement style is its second, and selecting it stops it being operator-driven.
    if (!ford.setDriveStyle(DriveStyle::GapCalibration))
    {
        return fail("a remote-driven car should accept GapCalibration");
    }
    if (ford.isManualByRemoteStyle())
    {
        return fail("GapCalibration should not count as the operator-driven style");
    }
    return true;
}

/** A measurement style must not let the operator's sliders reach the motor mid-run. */
bool aMeasurementStyleRefusesOperatorDriving() noexcept
{
    Control ford{false, true};
    if (!ford.setDriveStyle(DriveStyle::GapCalibration))
    {
        return fail("could not select GapCalibration");
    }
    if (!arm(ford, 1000U)) { return fail("could not arm Ford for a measurement"); }

    auto drive = commandOf(CommandType::Drive, 2U);
    drive.steeringCommand = 20.0F;
    drive.speedCommand = 50.0F;
    if (ford.handleCommand(drive, 1100U).accepted)
    {
        return fail("a measurement style accepted a Drive command");
    }
    const auto commands = ford.remoteDrive(1100U, 500U);
    if (commands.speedCommand != 0.0F || commands.steeringCommand != 0.0F)
    {
        return fail("a measurement style should get no operator commands");
    }
    return true;
}

/** A style that runs to its end disarms, and that is not a fault. */
bool finishingAStyleDisarmsWithoutAFault() noexcept
{
    Control ford{false, true};
    if (!ford.setDriveStyle(DriveStyle::GapCalibration)) { return fail("could not select the style"); }
    if (!arm(ford, 1000U)) { return fail("could not arm Ford"); }

    ford.finishDriveStyle();
    if (ford.controlState() != ControlState::Disarmed)
    {
        return fail("finishing a style should disarm the car");
    }
    if (ford.stateReason() != app::runtime::StateReason::DriveStyleFinished)
    {
        return fail("finishing should report its own reason, not a fault");
    }
    // Ready to be started again, with the same style still selected.
    if (ford.configuration().driveStyle != DriveStyle::GapCalibration)
    {
        return fail("finishing should leave the style selected");
    }
    if (!arm(ford, 2000U, 2U)) { return fail("a finished car should be startable again"); }
    return true;
}

/**
 * @brief Confirming a measured table is accepted only while disarmed, and only once.
 *
 * Disarmed is the point: the run that produced the table disarmed the car on its way to
 * finishing, so a command that required arming could never confirm anything.
 */
bool storingGapsIsConfirmedWhileDisarmed() noexcept
{
    Control ford{false, true};
    ford.setMqttConnected(true);

    if (ford.takeStoreGapsRequest()) { return fail("nothing should be pending at boot"); }

    const auto store = commandOf(CommandType::StoreGaps, 5U);
    if (!ford.handleCommand(store, 1000U).accepted)
    {
        return fail("a disarmed car should accept a store command");
    }
    if (!ford.takeStoreGapsRequest()) { return fail("the store request should be pending"); }
    if (ford.takeStoreGapsRequest()) { return fail("the request should be consumed by reading it"); }

    // A QoS-1 retransmission is acknowledged but must not store a second time.
    if (!ford.handleCommand(store, 1010U).accepted)
    {
        return fail("a retransmitted store should be acknowledged");
    }
    if (ford.takeStoreGapsRequest()) { return fail("a retransmission must not store twice"); }

    // Mid-run it is refused: the table is still being measured. The request id carries on
    // from the stores above, because Control tracks one sequence for every acknowledged
    // command rather than one per kind.
    if (!arm(ford, 2000U, 7U)) { return fail("could not arm Ford"); }
    if (ford.handleCommand(commandOf(CommandType::StoreGaps, 8U), 2100U).accepted)
    {
        return fail("an armed car should refuse a store command");
    }
    if (ford.takeStoreGapsRequest()) { return fail("a refused store must leave nothing pending"); }
    return true;
}

/** A car with distance sensors is the mirror image: autonomous styles, never the remote one. */
bool sensorCarRefusesTheRemoteStyle() noexcept
{
    Control vagrant{false, false};

    if (vagrant.configuration().driveStyle != DriveStyle::DecideAction)
    {
        return fail("a sensor car should start on DecideAction");
    }
    if (vagrant.isRemoteDrivenCar()) { return fail("Vagrant should not report itself remote-driven"); }
    if (vagrant.isManualByRemoteStyle()) { return fail("Vagrant should not be on the ManualByRemote style"); }

    if (vagrant.setDriveStyle(DriveStyle::ManualByRemote))
    {
        return fail("a sensor car accepted ManualByRemote");
    }
    if (!vagrant.setDriveStyle(DriveStyle::SlowLeft)) { return fail("a sensor car refused SlowLeft"); }
    return true;
}

/**
 * @brief A style is chosen only while disarmed.
 *
 * This is what makes "arming starts the selected style" answerable: the question of what
 * the car will do when started has one answer, fixed before anything can move.
 */
bool styleChangesOnlyWhileDisarmed() noexcept
{
    Control vagrant{false, false};

    if (!vagrant.setDriveStyle(DriveStyle::SlowLeft)) { return fail("disarmed style change refused"); }
    if (!arm(vagrant, 1000U)) { return fail("could not arm the car"); }
    if (vagrant.controlState() != ControlState::Armed) { return fail("car should be armed"); }

    if (vagrant.setDriveStyle(DriveStyle::SlowRight))
    {
        return fail("an armed car accepted a style change");
    }
    if (vagrant.configuration().driveStyle != DriveStyle::SlowLeft)
    {
        return fail("the refused change should leave the style alone");
    }
    // Re-selecting what is already selected is not a change, so it is allowed.
    if (!vagrant.setDriveStyle(DriveStyle::SlowLeft))
    {
        return fail("an armed car refused a no-op style change");
    }
    return true;
}

/**
 * @brief Drive commands and remoteDrive() follow the selected style, not the car.
 *
 * A car whose selected style is not the operator-driven one must refuse Drive commands
 * outright, so an operator's sliders cannot reach the motor while another style owns it.
 */
bool driveCommandsFollowTheSelectedStyle() noexcept
{
    Control vagrant{false, false}; // Selected style is DecideAction, not ManualByRemote.
    if (!arm(vagrant, 1000U)) { return fail("could not arm the sensor car"); }

    auto drive = commandOf(CommandType::Drive, 2U);
    drive.steeringCommand = 10.0F;
    drive.speedCommand = 20.0F;

    const auto result = vagrant.handleCommand(drive, 1100U);
    if (result.accepted) { return fail("a non-ManualByRemote style accepted a Drive command"); }
    if (result.error != CommandError::InvalidRequest)
    {
        return fail("a refused Drive command should report an invalid request");
    }

    const auto commands = vagrant.remoteDrive(1100U, 500U);
    if (commands.speedCommand != 0.0F || commands.steeringCommand != 0.0F)
    {
        return fail("remoteDrive should return nothing for a non-ManualByRemote style");
    }
    return true;
}

/**
 * @brief The operator's lease is enforced whatever style is selected.
 *
 * ManualByRemote gets this through remoteDrive(). A style that drives a script of its own
 * never calls remoteDrive, so it has to reach renewLease() instead - otherwise a console
 * that goes quiet while TCP stays up leaves a powered wheel turning for the whole run.
 */
bool theLeaseIsStyleIndependent() noexcept
{
    // A stale heartbeat disarms, on a car whose selected style is not ManualByRemote.
    Control vagrant{false, false};
    if (!arm(vagrant, 1000U)) { return fail("could not arm the sensor car"); }
    if (!vagrant.renewLease(1000U + Control::HeartbeatTimeoutMs - 1U))
    {
        return fail("the lease should hold just inside the heartbeat timeout");
    }
    if (vagrant.renewLease(1000U + Control::HeartbeatTimeoutMs))
    {
        return fail("a stale heartbeat should end the lease whatever the style");
    }
    if (vagrant.controlState() != ControlState::Disarmed)
    {
        return fail("a stale heartbeat should disarm");
    }
    if (vagrant.stateReason() != app::runtime::StateReason::HeartbeatTimeout)
    {
        return fail("a stale heartbeat should say so");
    }

    // A lost MQTT connection ends it too, and reports a different reason.
    Control ford{false, true};
    if (!arm(ford, 1000U)) { return fail("could not arm Ford"); }
    ford.setMqttConnected(false);
    if (ford.renewLease(1100U)) { return fail("a lost connection should end the lease"); }
    if (ford.stateReason() != app::runtime::StateReason::MqttDisconnected)
    {
        return fail("a lost connection should say so");
    }

    // A disarmed car has no lease to renew, and renewing must not revive one.
    Control idle{false, true};
    if (idle.renewLease(1000U)) { return fail("a disarmed car should have no lease"); }
    if (idle.controlState() != ControlState::Disarmed) { return fail("renewing should not arm"); }
    return true;
}

/** The operator-driven style accepts Drive commands and hands them back. */
bool theRemoteStyleAcceptsDriveCommands() noexcept
{
    Control ford{false, true};
    if (!arm(ford, 1000U)) { return fail("could not arm Ford"); }

    auto drive = commandOf(CommandType::Drive, 2U);
    drive.steeringCommand = -15.0F;
    drive.speedCommand = 30.0F;

    if (!ford.handleCommand(drive, 1100U).accepted)
    {
        return fail("ManualByRemote refused a valid Drive command");
    }

    const auto commands = ford.remoteDrive(1100U, 500U);
    if (commands.speedCommand != 30.0F || commands.steeringCommand != -15.0F)
    {
        return fail("ManualByRemote should hand back the operator's commands");
    }
    return true;
}
} // namespace

namespace test
{
bool runDriveStyleTest() noexcept
{
    if (!remoteDrivenCarSupportsOnlyItsOwnStyles()) { return false; }
    if (!aMeasurementStyleRefusesOperatorDriving()) { return false; }
    if (!finishingAStyleDisarmsWithoutAFault()) { return false; }
    if (!storingGapsIsConfirmedWhileDisarmed()) { return false; }
    if (!sensorCarRefusesTheRemoteStyle()) { return false; }
    if (!styleChangesOnlyWhileDisarmed()) { return false; }
    if (!driveCommandsFollowTheSelectedStyle()) { return false; }
    if (!theLeaseIsStyleIndependent()) { return false; }
    if (!theRemoteStyleAcceptsDriveCommands()) { return false; }

    std::printf("Drive style test succeeded!\n");
    return true;
}
} // namespace test
