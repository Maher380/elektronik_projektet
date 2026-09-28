/**
 * @file vagrant.h
 * @brief Vagrant: MP6550 brushed motor, frequency-steered servo, three IR sensors, A3144 odometer.
 */

#pragma once

#include "system/car/interface.h"

#include <cstdint>
#include <memory>

namespace driver::adc { class Interface; }
namespace driver::factory { class Interface; }
namespace driver::gpio { class Interface; }
namespace driver::ir_sensor { class Interface; }
namespace driver::pwm { class Interface; }

namespace app::car
{

/**
 * @brief The Vagrant car.
 */
class Vagrant final : public Interface
{
public:
    explicit Vagrant(driver::factory::Interface& factory) noexcept;
    ~Vagrant() noexcept override;

    bool init() noexcept override;
    void deinit() noexcept override;
    driver::motor::Interface* motor() noexcept override;
    driver::servo::Interface* steering() noexcept override;
    driver::odometer::Interface* odometer() noexcept override;
    bool readObstacleDistances(navigation::Distances& distances) noexcept override;
    void enableMotorOutput() noexcept override;
    void disableMotorOutput() noexcept override;
    bool handleSerialCommand(const char* command, const char* argument, bool hasArgument,
                             driver::serial::Interface& serial) noexcept override;
    const char* helpText() const noexcept override;
    void printMotorStatus(driver::serial::Interface& serial) const noexcept override;
    void fillPartTelemetry(communication::TelemetrySnapshot& snapshot) const noexcept override;

private:
    // IR sensors
    static constexpr std::uint8_t IrSensorForwardAdcPin{2U};    // A1
    static constexpr std::uint8_t IrSensorLeftAdcPin{1U};       // A0
    static constexpr std::uint8_t IrSensorRightAdcPin{4U};      // A3

    // MP6550
    static constexpr std::uint8_t mp6550MotorPwmForwardPin{5U};   // D2 / GPIO5
    static constexpr std::uint8_t mp6550MotorPwmBackwardPin{6U};  // D3 / GPIO6
    static constexpr std::uint8_t mp6550MotorSleepPin{7U};        // D4 / GPIO7

    // Steering servo
    static constexpr std::uint8_t steeringServoPwmPin{9U};        // D6 / GPIO9
    static constexpr std::uint32_t steeringServoPwmFrequencyHz{300U};

    // Odometer (A3144 Hall-effect sensor)
    static constexpr std::uint8_t odometerPin{18U};                       // D9 / GPIO18 (ADC2_CH7)
    static constexpr std::uint8_t odometerPulsesPerRevolution{2U};        // 2 magnets per wheel
    static constexpr float odometerWheelDiameterM{0.031F};                // 31 mm wheel

    std::unique_ptr<driver::pwm::Interface> myMotorForwardsPwm;
    std::unique_ptr<driver::pwm::Interface> myMotorBackwardsPwm;
    std::unique_ptr<driver::gpio::Interface> myMotorSleep;
    std::unique_ptr<driver::gpio::Interface> myOdometerGpio;
    std::unique_ptr<driver::adc::Interface> myIrSensorForwardAdc;
    std::unique_ptr<driver::adc::Interface> myIrSensorLeftAdc;
    std::unique_ptr<driver::adc::Interface> myIrSensorRightAdc;
    std::unique_ptr<driver::pwm::Interface> mySteeringServoPwm;
    std::unique_ptr<driver::motor::Interface> myMotor;
    std::unique_ptr<driver::ir_sensor::Interface> myIrSensorForward;
    std::unique_ptr<driver::ir_sensor::Interface> myIrSensorLeft;
    std::unique_ptr<driver::ir_sensor::Interface> myIrSensorRight;
    std::unique_ptr<driver::servo::Interface> mySteeringServo;
    std::unique_ptr<driver::odometer::Interface> myOdometer;
};

} // namespace app::car
