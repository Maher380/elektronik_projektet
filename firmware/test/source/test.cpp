#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "system/pin_manager/esp32s3.h"
#include "system/logic/reverse_controller.h"
#include "system/logic/motor_kickstart.h"
#include "test/a89301_programmer.h"
#include "test/pin_manager.h"

#include "driver/adc/stub.h"
#include "driver/factory/stub.h"
#include "driver/gpio/stub.h"
#include "driver/start_module/gpio.h"
#include "driver/distance_sensor/gp2y0a21yk.h"
#include "driver/motor/a89301.h"
#include "driver/motor/l298n.h"
#include "driver/motor/mp6550.h"
#include "driver/pwm/stub.h"

int main()
{
    using Direction = driver::motor::Direction;
    app::logic::MotorKickstart kickstart;
    if (kickstart.apply(10U, .44F, Direction::Forward, true) != 1.0F
        || kickstart.apply(109U, .44F, Direction::Forward, true) != 1.0F
        || kickstart.apply(110U, .44F, Direction::Forward, true) != .44F
        || kickstart.apply(120U, .5F, Direction::Forward, true) != .5F
        || kickstart.apply(130U, 0.0F, Direction::Forward, true) != 0.0F
        || kickstart.apply(140U, .44F, Direction::Forward, true) != .44F
        || kickstart.apply(150U, .4F, Direction::Backward, true) != .4F
        || kickstart.apply(160U, 0.0F, Direction::Backward, false) != 0.0F
        || kickstart.apply(170U, .2F, Direction::Backward, false) != .2F)
    {
        std::printf("Motor kickstart timing, stop, direction or manual bypass failed\n");
        return -1;
    }
    kickstart.arm();
    if (kickstart.apply(0xfffffff0U, .44F, Direction::Forward, true) != 1.0F
        || kickstart.apply(83U, .44F, Direction::Forward, true) != 1.0F
        || kickstart.apply(84U, .44F, Direction::Forward, true) != .44F)
    {
        std::printf("Motor kickstart timing, stop, direction or manual bypass failed\n");
        return -1;
    }
    using StartState = driver::start_module::State;
    driver::gpio::Stub startPin;
    driver::start_module::Gpio startModule{startPin};
    startPin.write(true);
    startModule.update(0U);
    startModule.update(100U);
    if (startModule.state() != StartState::Waiting) { return -1; }
    startPin.write(false);
    startModule.update(110U);
    startPin.write(true);
    startModule.update(120U);
    startModule.update(139U);
    if (startModule.state() != StartState::Waiting) { return -1; }
    startPin.write(false);
    startModule.update(140U);
    startPin.write(true);
    startModule.update(150U);
    startModule.update(170U);
    if (startModule.state() != StartState::Started) { return -1; }
    startPin.write(false);
    startModule.update(171U);
    startPin.write(true);
    startModule.update(200U);
    if (startModule.state() != StartState::Stopped) { return -1; }
    driver::start_module::Gpio wrapStart{startPin};
    startPin.write(false);
    wrapStart.update(0xfffffff0U);
    startPin.write(true);
    wrapStart.update(0xfffffff8U);
    wrapStart.update(12U);
    if (wrapStart.state() != StartState::Started) { return -1; }

    using Phase = app::logic::ReverseController::Phase;
    app::logic::ReverseController reverse;
    if (reverse.update(0U, 1U, true, false) != Phase::Braking
        || reverse.update(79U, 1U, true, false) != Phase::Braking
        || reverse.update(80U, 1U, true, false) != Phase::Settling
        || reverse.update(180U, 1U, true, false) != Phase::Reversing
        || std::fabs(reverse.reverseDuty(180U) - 0.4F) > 0.0001F
        || std::fabs(reverse.reverseDuty(230U) - 0.7F) > 0.0001F
        || reverse.reverseDuty(280U) != 1.0F
        || reverse.reverseDuty(380U) != 1.0F
        || reverse.reverseDuty(479U) != 1.0F
        || reverse.update(480U, 1U, true, false) != Phase::Recovering
        || reverse.reverseDuty(480U) != 0.0F
        || reverse.update(580U, 1U, true, false) != Phase::Holding
        || reverse.update(600U, 2U, true, false) != Phase::Holding
        || reverse.update(620U, 3U, true, false) != Phase::Braking
        || reverse.update(700U, 3U, true, false) != Phase::Settling
        || reverse.update(800U, 3U, true, false) != Phase::Reversing
        || std::fabs(reverse.reverseDuty(800U) - 0.4F) > 0.0001F
        || reverse.update(1100U, 3U, true, false) != Phase::Recovering
        || reverse.update(1200U, 3U, true, false) != Phase::Holding
        || reverse.update(1220U, 4U, true, false) != Phase::Holding
        || reverse.update(1240U, 5U, true, false) != Phase::Holding
        || reverse.update(1260U, 6U, true, false) != Phase::Holding
        || reverse.update(1280U, 7U, false, true) != Phase::Holding
        || reverse.update(1300U, 8U, false, true) != Phase::Ready
        || reverse.update(1320U, 9U, true, false) != Phase::Holding
        || reverse.update(1340U, 10U, false, true) != Phase::Holding
        || reverse.update(1360U, 11U, false, true) != Phase::Ready)
    {
        std::printf("Reverse retry or cancellation failed\n");
        return -1;
    }
    reverse.reset();
    if (reverse.update(0U, 1U, true, false) != Phase::Braking
        || reverse.update(80U, 2U, true, false) != Phase::Settling
        || reverse.update(180U, 3U, true, false) != Phase::Reversing
        || reverse.update(200U, 4U, false, true) != Phase::Reversing
        || reverse.update(220U, 5U, false, true) != Phase::Reversing
        || reverse.update(300U, 6U, false, false) != Phase::Reversing
        || reverse.update(380U, 7U, false, true) != Phase::Reversing
        || reverse.update(400U, 8U, false, true) != Phase::Recovering
        || reverse.update(500U, 9U, false, true) != Phase::Holding
        || reverse.update(500U, 9U, false, true) != Phase::Holding
        || reverse.update(520U, 10U, false, true) != Phase::Holding
        || reverse.update(540U, 11U, false, true) != Phase::Ready)
    {
        std::printf("Reverse clearance confirmation failed\n");
        return -1;
    }

    reverse.reset();
    if (reverse.update(0U, 1U, true, false, false) != Phase::Braking
        || reverse.update(80U, 2U, true, false, false) != Phase::Settling
        || reverse.update(500U, 3U, true, false, false) != Phase::Settling
        || reverse.update(510U, 4U, true, false, true) != Phase::Reversing
        || std::fabs(reverse.reverseDuty(510U) - 0.4F) > 0.0001F
        || reverse.update(809U, 5U, true, false, true) != Phase::Reversing
        || reverse.update(810U, 6U, true, false, true) != Phase::Recovering)
    {
        std::printf("Reverse steering preparation must not consume the pulse\n");
        return -1;
    }

    reverse.reset();
    reverse.update(0U, 1U, true, false);
    reverse.update(80U, 2U, true, false);
    reverse.update(180U, 3U, true, false);
    reverse.update(480U, 4U, true, false);
    reverse.update(580U, 5U, false, false);
    if (reverse.update(600U, 6U, false, false) != Phase::Holding
        || reverse.update(620U, 7U, false, false) != Phase::Braking)
    {
        std::printf("Intermediate clearance must not deadlock reverse recovery\n");
        return -1;
    }

    auto& pinManager = sys::pin_manager::Esp32s3::instance();

    if (!test::runPinManagerTest(pinManager)) { return -1; }
    if (!test::runA89301ProgrammerTest()) { return -1; }

    driver::adc::Stub testAdc;
    driver::distance_sensor::GP2Y0A21YK testSensor{testAdc};

    if (!std::isnan(testSensor.readDistance()))
    {
        std::printf("Uninitialized IR sensor should report an invalid reading\n");
        return -1;
    }

    if (!testAdc.init())
    {
        std::printf("ADC test init failed\n");
        return -1;
    }

    testAdc.simulateInput(2048U);

    const float distance = testSensor.readDistance();
    std::printf("IR sensor distance: %.2f cm\n", static_cast<double>(distance));

    if (distance <= 0.0F)
    {
        std::printf("IR sensor test failed\n");
        return -1;
    }

    driver::pwm::Stub testPwm;
    driver::gpio::Stub testInput1;
    driver::gpio::Stub testInput2;
    driver::motor::L298n testMotor{testPwm, testInput1, testInput2};

    if (!testMotor.init())
    {
        std::printf("L298N motor init failed\n");
        return -1;
    }

    if (!testMotor.setSpeed(0.7F))
    {
        std::printf("L298N motor speed test failed\n");
        return -1;
    }

    if ((testPwm.duty() != 0.7F) || !testInput1.read() || testInput2.read())
    {
        std::printf("L298N motor forward test failed\n");
        return -1;
    }

    if (!testMotor.setSpeed(0.3F) || (testPwm.duty() != 0.3F))
    {
        std::printf("L298N motor speed reduction test failed\n");
        return -1;
    }

    if (!testMotor.setDirection(driver::motor::Direction::Backward))
    {
        std::printf("L298N motor direction test failed\n");
        return -1;
    }

    if ((testPwm.duty() != 0.3F) || testInput1.read() || !testInput2.read())
    {
        std::printf("L298N motor backward output test failed\n");
        return -1;
    }

    if (!testMotor.stop(driver::motor::StopMode::Coast))
    {
        std::printf("L298N motor coast stop failed\n");
        return -1;
    }

    if ((testPwm.duty() != 0.0F) || testInput1.read() || testInput2.read())
    {
        std::printf("L298N motor coast output test failed\n");
        return -1;
    }

    if (!testMotor.setSpeed(0.5F) || !testMotor.stop(driver::motor::StopMode::Brake))
    {
        std::printf("L298N motor brake stop failed\n");
        return -1;
    }

    if ((testPwm.duty() != 1.0F) || !testInput1.read() || !testInput2.read())
    {
        std::printf("L298N motor brake output test failed\n");
        return -1;
    }

    if (!testMotor.deinit() || testMotor.isInitialized())
    {
        std::printf("L298N motor deinit failed\n");
        return -1;
    }

    driver::pwm::Stub bldcSpeedPwm;
    driver::gpio::Stub bldcDirection;
    driver::gpio::Stub bldcBrake;
    driver::motor::A89301 bldcMotor{bldcSpeedPwm, bldcDirection, bldcBrake};

    if (bldcMotor.setSpeed(0.5F))
    {
        std::printf("A89301 motor should reject speed before init\n");
        return -1;
    }

    if (!bldcMotor.init() || (bldcSpeedPwm.duty() != 0.0F) || !bldcDirection.read() || bldcBrake.read())
    {
        std::printf("A89301 motor init failed\n");
        return -1;
    }

    if (!bldcMotor.setSpeed(0.7F) || (bldcSpeedPwm.duty() != 0.7F) || bldcBrake.read())
    {
        std::printf("A89301 motor speed test failed\n");
        return -1;
    }

    if (bldcMotor.setSpeed(1.5F) || (bldcSpeedPwm.duty() != 0.7F))
    {
        std::printf("A89301 motor should reject out-of-range speed\n");
        return -1;
    }

    if (!bldcMotor.setDirection(driver::motor::Direction::Backward)
        || bldcDirection.read() || (bldcSpeedPwm.duty() != 0.7F))
    {
        std::printf("A89301 motor direction test failed\n");
        return -1;
    }

    if (!bldcMotor.stop(driver::motor::StopMode::Brake) || (bldcSpeedPwm.duty() != 0.0F) || !bldcBrake.read())
    {
        std::printf("A89301 motor brake stop failed\n");
        return -1;
    }

    if (!bldcMotor.setSpeed(0.4F) || (bldcSpeedPwm.duty() != 0.4F) || bldcBrake.read())
    {
        std::printf("A89301 motor brake release failed\n");
        return -1;
    }

    if (!bldcMotor.setSpeed(0.0F, driver::motor::StopMode::Coast)
        || (bldcSpeedPwm.duty() != 0.0F) || bldcBrake.read())
    {
        std::printf("A89301 motor coast stop failed\n");
        return -1;
    }

    if (!bldcMotor.deinit() || bldcMotor.isInitialized() || bldcSpeedPwm.isInitialized())
    {
        std::printf("A89301 motor deinit failed\n");
        return -1;
    }

    driver::pwm::Stub invertedSpeedPwm;
    driver::gpio::Stub invertedDirection;
    driver::gpio::Stub invertedBrake;
    driver::motor::A89301 invertedMotor{invertedSpeedPwm, invertedDirection, invertedBrake, true};

    if (!invertedMotor.init() || invertedDirection.read())
    {
        std::printf("A89301 inverted direction test failed\n");
        return -1;
    }

    driver::pwm::Stub forwardPwm;
    driver::pwm::Stub backwardPwm;
    driver::motor::MP6550 bridge{forwardPwm, backwardPwm};
    if (!bridge.init() || !bridge.setSpeed(0.5F)
        || forwardPwm.duty() != 0.5F || backwardPwm.duty() != 0.0F
        || !bridge.stop(driver::motor::StopMode::Brake)
        || forwardPwm.duty() != 1.0F || backwardPwm.duty() != 1.0F
        || !bridge.stop(driver::motor::StopMode::Coast)
        || forwardPwm.duty() != 0.0F || backwardPwm.duty() != 0.0F
        || !bridge.setDirection(driver::motor::Direction::Backward)
        || !bridge.setSpeed(1.0F)
        || forwardPwm.duty() != 0.0F || backwardPwm.duty() != 1.0F)
    {
        std::printf("MP6550 brake, coast, reverse sequence failed\n");
        return -1;
    }

    driver::factory::Stub testFactory;
    auto factoryPwmForward = testFactory.pwm(5U);
    auto factoryPwmBackward = testFactory.pwm(6U);

    if (!factoryPwmForward || !factoryPwmBackward)
    {
        std::printf("Factory PWM test failed\n");
        return -1;
    }

    auto factoryMotor = testFactory.motor(*factoryPwmForward, *factoryPwmBackward);
    if (!factoryMotor || !factoryMotor->init())
    {
        std::printf("Factory motor test failed\n");
        return -1;
    }

    std::array<std::unique_ptr<driver::adc::Interface>, 3U> factoryAdcs{
        testFactory.adc(1U),
        testFactory.adc(2U),
        testFactory.adc(4U),
    };

    for (auto& adc : factoryAdcs)
    {
        if (!adc || !adc->init() || !std::isfinite(adc->readVoltage()))
        {
            std::printf("Factory multi-channel ADC test failed\n");
            return -1;
        }
    }

    std::printf("All tests succeeded!\n");
    return 0;
}
