#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

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
    constexpr std::uint8_t LedPin{6U};
    constexpr std::uint32_t DefaultPeriodMs{500U};
    constexpr std::uint32_t SerialBaudRate{115200U};
    constexpr const char *WifiSsid{CONFIG_CNB_WIFI_SSID};
    constexpr const char *WifiPassword{CONFIG_CNB_WIFI_PASSWORD};
    

    constexpr std::size_t bufLen{192U};
    // @brief the sleep period between two ticks. 50 ms -> 20 Hz 
    constexpr int tickPeriod_ms{50U};

    // @brief how often the state should be logged to serial
    constexpr int logInterval_ms{1000};
    constexpr int logInterval_ticks{logInterval_ms/tickPeriod_ms};


} // namespace

namespace app::logic
{
Logic::~Logic() noexcept = default;

Logic::Logic(driver::factory::Interface& factory) noexcept
    : myMotorForwardsPwm{factory.pwm(mp6550MotorPwmForwardPin)}
    , myMotorBackwardsPwm{factory.pwm(mp6550MotorPwmBackwardPin)}
    , myMotorSleep{factory.gpioOutput(mp6550MotorSleepPin)}
    , mySerial({factory.serial(SerialBaudRate)})
{
    if (myMotorForwardsPwm && myMotorBackwardsPwm)
    {
        myMotor = factory.motor(*myMotorForwardsPwm, *myMotorBackwardsPwm);
    }

    for (std::size_t index{0U}; index < IrSensorCount; ++index)
    {
        // Create ADC, then create IR sensor with it if allocated.
        myIrSensorAdcs[index] = factory.adc(IrSensorAdcPins[index]);

        if (nullptr != myIrSensorAdcs[index])
        {
            myIrSensors[index] = factory.ir_sensor(*myIrSensorAdcs[index]);
        }
        else { break; }
    }

    setStartState();
    if (!initializeDrivers())
    {
        if (mySerial && mySerial->isInitialized())
        {
            mySerial->write("Initialization failed!\n");
        }

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
    myDistancesToObstacles.fill(std::numeric_limits<float>::quiet_NaN());
    myPlannedSpeed = 0.0F;

    // if (myLed) { myLed->write(false); }

    // if (myTimer)
    // {
    //     myTimer->setPeriod(myPeriodMs);
    //     myTimer->stop();
    // }
}

bool Logic::initializeDrivers() noexcept
{
    if (!mySerial)
    {
        return false;
    }

    if (!mySerial->isInitialized() && !mySerial->connect())
    {
        return false;
    }
    mySerial->write("CnB serial ready\n");

// #if CONFIG_CNB_ENABLE_WIFI
//     if (myWifi && !myWifi->isConnected())
//     {
//         myWifi->connect();
//     }
// #endif
    if (!myMotorForwardsPwm || !myMotorBackwardsPwm || !myMotorSleep || !myMotor)
    {
        return false;
    }

    for (std::size_t index{0U}; index < IrSensorCount; ++index)
    {
        if (!myIrSensorAdcs[index] || !myIrSensors[index])
        {
            return false;
        }

        if (!myIrSensorAdcs[index]->isInitialized() && !myIrSensorAdcs[index]->init())
        {
            return false;
        }

        if (!myIrSensors[index]->isInitialized())
        {
            return false;
        }
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

    return true;
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


void Logic::getEnvironmentPicture() noexcept
{
    for (std::size_t index{0U}; index < IrSensorCount; ++index)
    {
        if (myIrSensors[index] && myIrSensors[index]->isInitialized())
        {
            myDistancesToObstacles[index] = myIrSensors[index]->readDistance();
        }
        else
        {
            myDistancesToObstacles[index] = std::numeric_limits<float>::quiet_NaN();
        }
    }
}

void Logic::decideAction() noexcept
{
    const bool allDistancesValid = std::all_of(
        myDistancesToObstacles.begin(),
        myDistancesToObstacles.end(),
        [](const float distance) { return std::isfinite(distance) && (distance > 0.0F); });

    if (!allDistancesValid)
    {
        myPlannedSpeed = 0.0F;
        return;
    }

    const auto closestObstacle = std::min_element(
        myDistancesToObstacles.begin(), myDistancesToObstacles.end());
    myPlannedSpeed = (*closestObstacle < 30.0F) ? 0.0F : 0.5F;
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
    /**
     * in order to avoid to frequent logging and to reduce the noice in the distance
     * sum up all measurements done during a period ( maybe 1 s) and devide that value
     * by the number of measurements made. Print both average value and latest value.
     * note when shifting to next generation of logging (MQTT?), maybe something similar could be done
     */
    static std::array<double, IrSensorCount> accumulatedDistances{};
    static std::array<std::size_t, IrSensorCount> validSamples{};
    static std::size_t sampleCount{0U};

    for (std::size_t index{0U}; index < IrSensorCount; ++index)
    {
        if (std::isfinite(myDistancesToObstacles[index]))
        {
            accumulatedDistances[index] += myDistancesToObstacles[index];
            ++validSamples[index];
        }
    }

    ++sampleCount;
    if (sampleCount >= static_cast<std::size_t>(logInterval_ticks))
    {
        char buf[bufLen]{'\0'};
        std::array<double, IrSensorCount> averages{};

        for (std::size_t index{0U}; index < IrSensorCount; ++index)
        {
            averages[index] = validSamples[index] > 0U
                ? accumulatedDistances[index] / static_cast<double>(validSamples[index])
                : std::numeric_limits<double>::quiet_NaN();
        }

        std::snprintf(
            buf,
            sizeof(buf),
            "IR cm L: %.2f (avg %.2f), C: %.2f (avg %.2f), R: %.2f (avg %.2f)\n",
            static_cast<double>(myDistancesToObstacles[0]), averages[0],
            static_cast<double>(myDistancesToObstacles[1]), averages[1],
            static_cast<double>(myDistancesToObstacles[2]), averages[2]);

        if (mySerial && mySerial->isInitialized())
        {
            mySerial->write(buf);
        }

        accumulatedDistances.fill(0.0);
        validSamples.fill(0U);
        sampleCount = 0U;
    }
}


void Logic::run(const std::atomic<bool>& stop) noexcept
{
    while (!stop.load())
    {
        getEnvironmentPicture();
        decideAction();
        executeAction();
        logState();

        vTaskDelay(pdMS_TO_TICKS(tickPeriod_ms));
    }

    // if (myMotor)
    // {
    //     myMotor->stop(driver::motor::StopMode::Coast);
    // }
}

} // namespace app::logic
