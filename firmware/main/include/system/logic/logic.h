/**
 * @file logic.h
 * @brief Declaration of the main application logic.
 */

#pragma once

#include "driver/factory/interface.h"
#include "driver/motor/interface.h"
#include "driver/serial/interface.h"

#include "system/car/interface.h"
#include "system/communication/manager.h"
#include "system/runtime/control.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace app::logic {

enum class DriverStyle : std::uint8_t
{
    DecideAction,
    SlowLeft,
    SlowRight,
    GradualSweep,
    ManualBySerial,
};

struct PlannedAction
{
    float speed{0.0F};
    driver::motor::Direction direction{driver::motor::Direction::Forward};
    float steeringDegrees{0.0F};
    driver::motor::StopMode stopMode{driver::motor::StopMode::Coast};
};

/**
 * @brief Main system logic for the autonomous car starter application.
 *
 * The logic layer uses the car's parts and stays independent from ESP-IDF
 * implementation details and from which car it runs on.
 */
class Logic final {
public:

    Logic(driver::factory::Interface& factory, car::Interface& car) noexcept;
    ~Logic() noexcept;

    void run(const std::atomic<bool>& stop) noexcept;
    void setDriverStyle(DriverStyle style) noexcept;

    Logic(const Logic&) = delete;
    Logic& operator=(const Logic&) = delete;
    Logic(Logic&&) = delete;
    Logic& operator=(Logic&&) = delete;

private:
    // MQTT-only integration; original SCRUM-16 members and methods follow.
    void initializeMqttOverlay() noexcept;
    void processMqttOverlay(std::uint32_t nowMs) noexcept;
    bool authorizeMqttAction(std::uint32_t nowMs) noexcept;
    void publishMqttTelemetry(std::uint32_t nowMs) noexcept;
    void disableMqttOutput() noexcept;
    void shutdownMqttOverlay() noexcept;

    void writeHelp() noexcept;
    void setStartState() noexcept;
    bool initializeDrivers() noexcept;
    void deinitializeDrivers() noexcept;
    void processWifi() noexcept;
    void processTimer() noexcept;

    /**
     * @brief Read and apply a manual motor command from serial, if one is waiting.
     * Recognized lines: "SPEED <0-1>", "FORWARD", "BACKWARD", "BRAKE", "COAST", "STOP", "AUTO",
     * "PWMDUTYFWD <0-100>", "PWMDUTYBWD <0-100>", "DRIVESTYLE <style>", "LOG <ON|OFF>", "HELP".
     * Any command other than AUTO, DRIVESTYLE, LOG or HELP switches myDriverStyle to
     * ManualBySerial so the sensor-based decideAction() stops overriding the commanded state.
     */
    void processSerialCommand() noexcept;

    /**
     * @brief Get a picture of the environment.
     * makes use of relevant sensors and stores the data in member variables for further processing.
     */
    void getEnvironmentPicture() noexcept;
    bool hasValidEnvironmentPicture() const noexcept;

    /**
     * @brief Decide on the next action based on the environment picture.
     * Analyzes the data from getEnvironmentPicture() and determines the appropriate action to take.
     */
    void decideAction() noexcept;
    void decideNormalAction() noexcept;
    void decideGradualSweepAction() noexcept;
    void decideSlowLeftAction() noexcept;
    void decideSlowRightAction() noexcept;

    /**
     * @brief Execute the decided action.
     * Carries out the action determined by decideAction(), such as controlling motors or other actuators.
     */
    void executeAction() noexcept;

    /**
     * @brief Log the current state of the system.
     * Records the state information for debugging and monitoring purposes.
     * Depending on configuration these logs are written to file for debugging later or transmitted over WiFi to a remote server for real-time monitoring.
     * The logging mechanism is designed to be efficient and non-blocking to avoid interfering with the main logic loop.
     * The logState() function can be called at regular intervals or triggered by specific events in the system to capture relevant state information.
     * The logged data can include sensor readings, motor states, WiFi connection status, and any other relevant information that helps in understanding the system's behavior.
     * The logState() function is intended to be flexible and can be extended to include additional
     */
    void logState() noexcept;


    car::Interface& myCar;
    std::unique_ptr<driver::serial::Interface> mySerial;

    bool myBlinkEnabled{false};
    bool myLogEnabled{false};
    // Set by processSerialCommand() when a motor command was issued; consumed
    // in run() after executeAction() so the printed PWM duty reflects the
    // value actually just written to hardware, not the previous tick's.
    bool myMotorCommandPending{false};
    std::uint32_t myPeriodMs{500U};

    float myDistanceToObstacleForward{0.0F};
    float myDistanceToObstacleLeft{0.0F};
    float myDistanceToObstacleRight{0.0F};

    // planned action data members can be added here for storing the decided action, etc.
    // For example, you might have an enum or struct to represent the action to be taken
    DriverStyle myDriverStyle{DriverStyle::DecideAction};
    float mySweepSteeringDegrees{-90.0F};
    float mySweepDirection{1.0F};
    PlannedAction myPlannedAction{};

    // MQTT parameters; all route/steering decisions remain in logic.cpp.
    float myStopDistanceCm{30.0F};
    float myDriveDuty{0.5F};

    app::communication::Manager myCommunication;
    app::runtime::Control myRuntimeControl;
    float myMqttAppliedSpeed{0.0F};
};

} // namespace app::logic
