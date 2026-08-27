/**
 * @file logic.h
 * @brief Declaration of the main application logic.
 */

#pragma once

#include "driver/factory/interface.h"
#include "driver/adc/interface.h"
#include "driver/ir_sensor/interface.h"
#include "driver/motor/interface.h"
#include "driver/pwm/interface.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace app::logic {

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

    Logic(const Logic&) = delete;
    Logic& operator=(const Logic&) = delete;
    Logic(Logic&&) = delete;
    Logic& operator=(Logic&&) = delete;

private:
    void setStartState() noexcept;
    bool initializeDrivers() noexcept;
    void processWifi() noexcept;
    void processTimer() noexcept;
    void processDistance() noexcept;

    /**
     * @brief Get a picture of the environment.
     * makes use of relevant sensors and stores the data in member variables for further processing. 
     */
    void getEnvironmentPicture() noexcept;

    /**
     * @brief Decide on the next action based on the environment picture.
     * Analyzes the data from getEnvironmentPicture() and determines the appropriate action to take.
     */
    void decideAction() noexcept;

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


    static constexpr std::uint8_t IrSensorAdcPin{1};       // A0


    // l298 Motor
    static constexpr std::uint8_t l298MotorPwm{2};      // 
    static constexpr std::uint8_t l298MotorGpio1{3};    //
    static constexpr std::uint8_t l298MotorGpio2{4};    // 

    // M6550
    static constexpr std::uint8_t mp6550MotorPwmForwardPin{5U};   // D2 / GPIO5
    static constexpr std::uint8_t mp6550MotorPwmBackwardPin{6U};  // D3 / GPIO6
    static constexpr std::uint8_t mp6550MotorSleepPin{7U};        // D4 / GPIO7


    std::unique_ptr<driver::pwm::Interface> myMotorForwardsPwm;
    std::unique_ptr<driver::pwm::Interface> myMotorBackwardsPwm;
    std::unique_ptr<driver::adc::Interface> myIrSensorAdc;
    std::unique_ptr<driver::motor::Interface> myMotor;
    std::unique_ptr<driver::ir_sensor::Interface> myIrSensor;


    bool myBlinkEnabled{false};
    std::uint32_t myPeriodMs{500U};

    // Environment picture data members can be added here for storing sensor readings, etc.
    float myDistanceToObstacle{0.0f}; // Example member variable to store distance to an obstacle


    // planned action data members can be added here for storing the decided action, etc.
    // For example, you might have an enum or struct to represent the action to be taken
    float myPlannedSpeed{0.0f}; // Example member variable to store planned speed
};

} // namespace app::logic
