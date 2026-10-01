#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "system/pin_manager/esp32s3.h"
#include "system/logic/reverse_controller.h"
#include "test/a89301_programmer.h"
#include "test/pin_manager.h"

#include "driver/adc/stub.h"
#include "driver/factory/stub.h"
#include "driver/gpio/stub.h"
#include "driver/distance_sensor/gp2y0a21yk.h"
#include "driver/motor/a89301.h"
#include "driver/motor/l298n.h"
#include "driver/motor/mp6550.h"
#include "driver/pwm/stub.h"

int main()
{
    using Phase = app::logic::ReverseController::Phase;
    app::logic::ReverseController reverse;
    if (reverse.update(0U, 1U, true, false) != Phase::Braking
        || reverse.update(79U, 1U, true, false) != Phase::Braking
        || reverse.update(80U, 1U, true, false) != Phase::Settling
        || reverse.update(180U, 1U, true, false) != Phase::Reversing
        || reverse.update(480U, 1U, true, false) != Phase::Recovering
        || reverse.update(580U, 1U, true, false) != Phase::Holding
        || reverse.update(600U, 2U, true, false) != Phase::Holding
        || reverse.update(610U, 2U, true, false) != Phase::Holding
        || reverse.update(620U, 3U, true, false) != Phase::Braking
        || reverse.update(700U, 3U, true, false) != Phase::Settling
        || reverse.update(800U, 3U, true, false) != Phase::Reversing
        || reverse.update(1100U, 3U, true, false) != Phase::Recovering
        || reverse.update(1200U, 3U, true, false) != Phase::Holding
        || reverse.update(1220U, 4U, true, false) != Phase::Holding
        || reverse.update(1240U, 5U, true, false) != Phase::Holding
        || reverse.update(1260U, 6U, true, false) != Phase::Holding
        || reverse.update(1280U, 7U, false, true) != Phase::Holding
        || reverse.update(1300U, 8U, false, true) != Phase::Ready
        || reverse.update(1320U, 9U, true, false) != Phase::Braking)
    {
        std::printf("Reverse retry sequence failed\n");
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
