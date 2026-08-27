#include <cstdio>

#include "system/logic/logic.h"

#include "driver/adc/interface.h"
#include "driver/gpio/interface.h"
#include "driver/ir_sensor/interface.h"
#include "driver/motor/interface.h"
#include "driver/serial/interface.h"
#include "driver/timer/interface.h"
#include "driver/wifi/interface.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr std::uint8_t AdcPin{1U};
    constexpr std::uint8_t LedPin{6U};
    constexpr std::uint32_t DefaultPeriodMs{500U};
    constexpr std::uint32_t SerialBaudRate{115200U};
    constexpr const char *WifiSsid{CONFIG_CNB_WIFI_SSID};
    constexpr const char *WifiPassword{CONFIG_CNB_WIFI_PASSWORD};

    constexpr std::uint8_t bufLen{64U};
} // namespace

namespace app::logic
{
Logic::~Logic() noexcept = default;

Logic::Logic(driver::factory::Interface& factory) noexcept
    : myMotorForwardsPwm{factory.pwm(mp6550MotorPwmForwardPin)}
    , myMotorBackwardsPwm{factory.pwm(mp6550MotorPwmBackwardPin)}
    , myMotorSleep{factory.gpioOutput(mp6550MotorSleepPin)}
    , myIrSensorAdc{factory.adc(IrSensorAdcPin)}
{
    if (myMotorForwardsPwm && myMotorBackwardsPwm)
    {
        myMotor = factory.motor(*myMotorForwardsPwm, *myMotorBackwardsPwm);
    }

    if (myIrSensorAdc)
    {
        myIrSensor = factory.ir_sensor(*myIrSensorAdc);
    }

    setStartState();
    if (!initializeDrivers())
    {
        while (true)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }
}

void Logic::setStartState() noexcept
{
    myBlinkEnabled = false;
    myPeriodMs = DefaultPeriodMs;

    // if (myLed) { myLed->write(false); }

    // if (myTimer)
    // {
    //     myTimer->setPeriod(myPeriodMs);
    //     myTimer->stop();
    // }
}

bool Logic::initializeDrivers() noexcept
{
    if (!myMotorForwardsPwm || !myMotorBackwardsPwm || !myMotorSleep ||
        !myIrSensorAdc || !myIrSensor || !myMotor)
    {
        return false;
    }

    if (!myIrSensorAdc->isInitialized() && !myIrSensorAdc->init())
    {
        return false;
    }

    if (!myMotorSleep->isInitialized())
    {
        return false;
    }

    myMotorSleep->write(true); // nSLEEP_HB HIGH keeps MP6550 awake.

    if (!myMotor->isInitialized() && !myMotor->init())
    {
        return false;
    }

    return myIrSensor->isInitialized();
}

void Logic::processWifi() noexcept
{
// #if CONFIG_CNB_ENABLE_WIFI
//     if (myWifi && myWifi->isInitialized() && !myWifi->isConnected())
//     {
//         myWifi->reconnect();
//     }
// #endif
}

void Logic::processTimer() noexcept
{
    // if (myBlinkEnabled && myLed && myTimer && myTimer->isTimeout())
    // {
    //     myLed->toggle();
    // }
}

void Logic::processDistance() noexcept
{
    // const auto distance = myIr->readDistance();
    // // Testa att skriva ut distansen, kolla att den ser rimlit ut (sanity check).
    // char buf[bufLen]{'\0'};
    // std::snprintf(buf, sizeof(buf), "Distance: %.2f\n", static_cast<double>(distance));
    // mySerial->write(buf);
}


void Logic::getEnvironmentPicture() noexcept
{
    if (myIrSensor && myIrSensor->isInitialized())
    {
        myDistanceToObstacle = myIrSensor->readDistance();
        return;
    }

    myDistanceToObstacle = 0.0F;
}

void Logic::decideAction() noexcept
{
    if(myDistanceToObstacle < 30.0f) // Example threshold for obstacle detection
    {
        myPlannedSpeed = 0.0f; // Stop if too close to an obstacle
    }
    else
    {
        myPlannedSpeed = 1.0f; // Move forward at a speed of 1 m/s
    }
}

void Logic::executeAction() noexcept
{
    if (!myMotor || !myMotor->isInitialized())
    {
        return;
    }

    myMotor->setDirection(driver::motor::Direction::Forward);
    myMotor->setSpeed(myPlannedSpeed, driver::motor::StopMode::Coast);
}

void Logic::logState() noexcept
{
    ;
}


void Logic::run(const std::atomic<bool>& stop) noexcept
{
    while (!stop.load())
    {
        getEnvironmentPicture();
        decideAction();
        executeAction();
        logState();

        vTaskDelay(pdMS_TO_TICKS(50U));
    }

    if (myMotor)
    {
        myMotor->stop(driver::motor::StopMode::Coast);
    }
}

} // namespace app::logic
