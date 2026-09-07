/**
 * @file logic.h
 * @brief Declaration of the main application logic.
 */

#pragma once

#include "driver/factory/interface.h"
#include "driver/adc/interface.h"
#include "driver/gpio/interface.h"
#include "driver/ir_sensor/interface.h"
#include "driver/motor/interface.h"
#include "driver/pwm/interface.h"
#include "driver/servo/interface.h"

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
};

struct PlannedAction
{
    float speed{0.0F};
    float steeringDegrees{0.0F};
    driver::motor::StopMode stopMode{driver::motor::StopMode::Coast};
};

/**
 * @brief Main system logic for the autonomous car starter application.
 *
 * The logic layer owns driver interfaces and stays independent from ESP-IDF
 * implementation details.
 */
class Logic final {
public:

    explicit Logic(driver::factory::Interface& factory) noexcept;
    ~Logic() noexcept;

    void run(const std::atomic<bool>& stop) noexcept;
    void setDriverStyle(DriverStyle style) noexcept;

    Logic(const Logic&) = delete;
    Logic& operator=(const Logic&) = delete;
    Logic(Logic&&) = delete;
    Logic& operator=(Logic&&) = delete;

private:
    void setStartState() noexcept;
    bool initializeDrivers() noexcept;
    void deinitializeDrivers() noexcept;
    void processWifi() noexcept;
    void processTimer() noexcept;

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


    static constexpr std::uint8_t IrSensorForwardAdcPin{2U};    // A1
    static constexpr std::uint8_t IrSensorLeftAdcPin{1U};       // A0
    static constexpr std::uint8_t IrSensorRightAdcPin{4U};      // A3


    // l298 Motor
    static constexpr std::uint8_t l298MotorPwm{2};      //
    static constexpr std::uint8_t l298MotorGpio1{3};    //
    static constexpr std::uint8_t l298MotorGpio2{4};    //

    // M6550
    static constexpr std::uint8_t mp6550MotorPwmForwardPin{5U};   // D2 / GPIO5
    static constexpr std::uint8_t mp6550MotorPwmBackwardPin{6U};  // D3 / GPIO6
    static constexpr std::uint8_t mp6550MotorSleepPin{7U};        // D4 / GPIO7
    static constexpr std::uint8_t steeringServoPwmPin{9U};        // D6 /


    std::unique_ptr<driver::pwm::Interface> myMotorForwardsPwm;
    std::unique_ptr<driver::pwm::Interface> myMotorBackwardsPwm;
    std::unique_ptr<driver::gpio::Interface> myMotorSleep;
    std::unique_ptr<driver::adc::Interface> myIrSensorForwardAdc;
    std::unique_ptr<driver::adc::Interface> myIrSensorLeftAdc;
    std::unique_ptr<driver::adc::Interface> myIrSensorRightAdc;
    std::unique_ptr<driver::motor::Interface> myMotor;
    std::unique_ptr<driver::ir_sensor::Interface> myIrSensorForward;
    std::unique_ptr<driver::ir_sensor::Interface> myIrSensorLeft;
    std::unique_ptr<driver::ir_sensor::Interface> myIrSensorRight;
    std::unique_ptr<driver::serial::Interface> mySerial;
    std::unique_ptr<driver::pwm::Interface> mySteeringServoPwm;
    std::unique_ptr<driver::servo::Interface> mySteeringServo;

    bool myBlinkEnabled{false};
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
};

} // namespace app::logic
