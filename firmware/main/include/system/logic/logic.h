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
};

} // namespace app::logic
