/**
 * @file interface.h
 * @brief Abstract interface for a car: its parts and car-specific extras.
 */

#pragma once

#include "system/navigation/types.h"

namespace driver::motor { class Interface; }
namespace driver::odometer { class Interface; }
namespace driver::serial { class Interface; }
namespace driver::servo { class Interface; }
namespace app::communication { struct TelemetrySnapshot; }

namespace app::car
{

/**
 * @brief A car owns the drivers for its parts.
 *
 * A part the car does not have returns nullptr or false. The extras have empty
 * defaults and are only overridden by cars with part-specific commands or data.
 */
class Interface
{
public:
    virtual ~Interface() noexcept = default;

    /**
     * @brief Initialize the car's parts.
     *
     * @return True if every part was initialized, false otherwise.
     */
    virtual bool init() noexcept = 0;

    /**
     * @brief Deinitialize the car's parts.
     */
    virtual void deinit() noexcept = 0;

    /** @return The motor, or nullptr if the car has none. */
    virtual driver::motor::Interface* motor() noexcept = 0;

    /** @return The steering, or nullptr if the car has none. */
    virtual driver::servo::Interface* steering() noexcept = 0;

    /** @return The odometer, or nullptr if the car has none. */
    virtual driver::odometer::Interface* odometer() noexcept = 0;

    /**
     * @brief Read the distances to obstacles in cm.
     *
     * @param[out] distances Distances per direction, NaN where not measured.
     * @return True if the distances were read, false if the car cannot measure them.
     */
    virtual bool readObstacleDistances(navigation::Distances& distances) noexcept = 0;

    /** Power the motor output before an authorized action. */
    virtual void enableMotorOutput() noexcept {}

    /** Remove power from the motor output whenever control is lost. */
    virtual void disableMotorOutput() noexcept {}

    /**
     * @brief Handle a serial command that the shared logic does not know.
     *
     * @param[in] command Upper-case command word.
     * @param[in] argument Upper-case argument, empty if none.
     * @param[in] hasArgument True if an argument was given.
     * @param[in] serial Serial port for replies.
     * @return True if the command was handled.
     */
    virtual bool handleSerialCommand(const char* command, const char* argument, bool hasArgument,
                                     driver::serial::Interface& serial) noexcept
    {
        (void)command;
        (void)argument;
        (void)hasArgument;
        (void)serial;
        return false;
    }

    /** @return Help lines for the car's own serial commands, each ending in a newline. */
    virtual const char* helpText() const noexcept { return ""; }

    /** Print the car's part-specific motor status. */
    virtual void printMotorStatus(driver::serial::Interface& serial) const noexcept { (void)serial; }

    /** Fill the part-specific telemetry fields the car has. */
    virtual void fillPartTelemetry(communication::TelemetrySnapshot& snapshot) const noexcept { (void)snapshot; }

    Interface(const Interface&) = delete;
    Interface& operator=(const Interface&) = delete;
    Interface(Interface&&) = delete;
    Interface& operator=(Interface&&) = delete;

protected:
    Interface() noexcept = default;
};

} // namespace app::car
