#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "system/pin_manager/esp32s3.h"
#include "test/pin_manager.h"

#include "driver/adc/stub.h"
#include "driver/factory/stub.h"
#include "driver/gpio/stub.h"
#include "driver/ir_sensor/esp32s3.h"
#include "driver/motor/l298n.h"
#include "driver/pwm/stub.h"

int main()
{
    auto& pinManager = sys::pin_manager::Esp32s3::instance();

    if (!test::runPinManagerTest(pinManager)) { return -1; }

    driver::adc::Stub testAdc;
    driver::ir_sensor::Esp32s3 testSensor{testAdc};

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
