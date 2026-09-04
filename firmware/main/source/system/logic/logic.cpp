#include <cstdio>

#include <algorithm>

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
    // @brief the sleep period between two ticks. 50 ms -> 20 Hz 
    constexpr int tickPeriod_ms{50U};

    // @brief how often the state should be logged to serial
    constexpr int logInterval_ms{1000};
    constexpr int logInterval_ticks{logInterval_ms/tickPeriod_ms};


} // namespace

namespace app::logic
{
Logic::~Logic() noexcept = default;

void Logic::setDriverStyle(const DriverStyle style) noexcept
{
    myDriverStyle = style;
}

Logic::Logic(driver::factory::Interface& factory) noexcept
    : myMotorForwardsPwm{factory.pwm(mp6550MotorPwmForwardPin)}
    , myMotorBackwardsPwm{factory.pwm(mp6550MotorPwmBackwardPin)}
    , myMotorSleep{factory.gpioOutput(mp6550MotorSleepPin)}
    , myIrSensorForwardAdc{factory.adc(IrSensorForwrdAdcPin)}
    , myIrSensorLeftAdc{factory.adc(IrSensorLeftAdcPin)}
    , myIrSensorRightAdc{factory.adc(IrSensorRightAdcPin)}
    , mySerial({factory.serial(SerialBaudRate)})
    , mySteeringServoPwm{factory.pwm(steeringServoPwmPin)}
{
    if (myMotorForwardsPwm && myMotorBackwardsPwm)
    {
        myMotor = factory.motor(*myMotorForwardsPwm, *myMotorBackwardsPwm);
    }

    if (myIrSensorForwardAdc)
    {
        myIrSensorForward = factory.ir_sensor(*myIrSensorForwardAdc);
    }
    if (myIrSensorLeftAdc)
    {
        myIrSensorLeft = factory.ir_sensor(*myIrSensorLeftAdc);
    }
    if (myIrSensorRightAdc)
    {
        myIrSensorRight = factory.ir_sensor(*myIrSensorRightAdc);
    }
    if (mySteeringServoPwm)
    {
        mySteeringServo = factory.servo(*mySteeringServoPwm);
    }

    setStartState();
    if (!initializeDrivers())
    {
        while (true)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
        // Skriv ut eller indikera via en LED att initieringen misslyckades.
        if (mySerial)
        {
            mySerial->write("Initialization failed!\n");
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
    if (mySerial &&mySerial->connect())
    {
        mySerial->write("CnB serial ready\n");
    }
    else
    {
        return false;
    }


// #if CONFIG_CNB_ENABLE_WIFI
//     if (myWifi && !myWifi->isConnected())
//     {
//         myWifi->connect();
//     }
// #endif
    // Verify that all required driver objects were created.
    if (!myMotorForwardsPwm || 
        !myMotorBackwardsPwm || 
        !myMotorSleep ||
        !myIrSensorForwardAdc || 
        !myIrSensorLeftAdc || 
        !myIrSensorRightAdc || 
        !myMotor || 
        !myIrSensorForward ||
        !myIrSensorLeft || 
        !myIrSensorRight ||
        !mySteeringServoPwm ||
        !mySteeringServo ||
        !mySerial )
    {
        return false;
    }

    // Initialize drivers that expose an explicit init operation.
    // The GPIO output is initialized by its constructor.
    // IR sensors become ready when their ADC dependencies are initialized.
    // Serial is prepared through connect() above.
    myMotorForwardsPwm->init();
    myMotorBackwardsPwm->init();
    // no init function for myMotorSleep
    myIrSensorForwardAdc->init();
    myIrSensorLeftAdc->init();
    myIrSensorRightAdc->init();
    myMotor->init();
    mySteeringServoPwm->init();
    mySteeringServo->init();
    // no init function for myIrSensorLeft
    // no init function for myIrSensorRight
    // no init function for mySerial

    // Verify that all required drivers are initialized and ready.
    if (!myMotorForwardsPwm->isInitialized() ||
        !myMotorBackwardsPwm->isInitialized() ||
        !myMotorSleep->isInitialized() ||
        !myIrSensorForwardAdc->isInitialized() ||
        !myIrSensorLeftAdc->isInitialized() ||
        !myIrSensorRightAdc->isInitialized() ||
        !myMotor->isInitialized() ||
        !myIrSensorForward->isInitialized() ||
        !myIrSensorLeft->isInitialized() ||
        !myIrSensorRight->isInitialized() ||
        !mySteeringServoPwm->isInitialized() ||
        !mySteeringServo->isInitialized() ||
        !mySerial->isInitialized())
    {
        return false;
    }

    myMotorSleep->write(true); // nSLEEP_HB HIGH keeps MP6550 awake.

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
    //Check if all sensors are functional
    if (myIrSensorForward && myIrSensorForward->isInitialized() &&
        myIrSensorLeft    && myIrSensorLeft->isInitialized()    &&
        myIrSensorRight   && myIrSensorRight->isInitialized()   )
    {
        myDistanceToObstacleForward = myIrSensorForward->readDistance();
        myDistanceToObstacleLeft    = myIrSensorLeft->readDistance();
        myDistanceToObstacleRight   = myIrSensorRight->readDistance();
        return;
    }

    myDistanceToObstacleForward = 0.0F;
    myDistanceToObstacleLeft = 0.0F;
    myDistanceToObstacleRight = 0.0F;
}

void Logic::decideAction() noexcept
{
    switch (myDriverStyle)
    {
    case DriverStyle::DecideAction:
        decideNormalAction();
        break;
    case DriverStyle::SlowLeft:
        decideSlowLeftAction();
        break;
    case DriverStyle::SlowRight:
        decideSlowRightAction();
        break;
    }
}

void Logic::decideNormalAction() noexcept
{
    float distanceToClosestObject;
    if (myDistanceToObstacleForward > std::min(myDistanceToObstacleLeft, myDistanceToObstacleRight))
    {
        myPlannedAction.steeringDegrees = 0.0F;
        distanceToClosestObject = myDistanceToObstacleForward;
    }
    else if (myDistanceToObstacleLeft > myDistanceToObstacleRight)
    {
        myPlannedAction.steeringDegrees = -5.0F;
        distanceToClosestObject = myDistanceToObstacleLeft;
    }
    else
    {
        myPlannedAction.steeringDegrees = 5.0F;
        distanceToClosestObject = myDistanceToObstacleRight;
    }

    if (distanceToClosestObject < 30.0F)
    {
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
    }
    else
    {
        myPlannedAction.speed = 0.5F;
        myPlannedAction.stopMode = driver::motor::StopMode::Coast;
    }
}

void Logic::decideSlowLeftAction() noexcept
{
    myPlannedAction.steeringDegrees = -90.0F;
    if (std::min({myDistanceToObstacleForward, myDistanceToObstacleLeft, myDistanceToObstacleRight}) < 30.0F)
    {
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
    }
    else
    {
        myPlannedAction.speed = 0.2F;
        myPlannedAction.stopMode = driver::motor::StopMode::Coast;
    }
}

void Logic::decideSlowRightAction() noexcept
{
    myPlannedAction.steeringDegrees = 90.0F;
    if (std::min({myDistanceToObstacleForward, myDistanceToObstacleLeft, myDistanceToObstacleRight}) < 30.0F)
    {
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
    }
    else
    {
        myPlannedAction.speed = 0.2F;
        myPlannedAction.stopMode = driver::motor::StopMode::Coast;
    }
}

void Logic::executeAction() noexcept
{
    if (!myMotor || !myMotor->isInitialized())
    {
        return;
    }

    if (mySteeringServo && mySteeringServo->isInitialized())
    {
        mySteeringServo->setDirection(myPlannedAction.steeringDegrees);
    }

    if (myPlannedAction.speed > 0.0F)
    {
        myMotor->setDirection(driver::motor::Direction::Forward);
        myMotor->setSpeed(myPlannedAction.speed, myPlannedAction.stopMode);
    }
    else
    {
        myMotor->stop(myPlannedAction.stopMode);
    }
}

void Logic::logState() noexcept
{
    /**
     * in order to avoid to frequent logging and to reduce the noice in the distance
     * sum up all measurements done during a period ( maybe 1 s) and devide that value
     * by the number of measurements made. Print both average value and latest value.
     * note when shifting to next generation of logging (MQTT?), maybe something similar could be done
     */
    static int i = 0;
    static double accumulatedDistanceForward{0.0f};
    static double accumulatedDistanceLeft{0.0f};
    static double accumulatedDistanceRight{0.0f};

    accumulatedDistanceForward += myDistanceToObstacleForward;
    accumulatedDistanceLeft += myDistanceToObstacleLeft;
    accumulatedDistanceRight += myDistanceToObstacleRight;
    if(0 == i++%logInterval_ticks)
    {
        char buf[bufLen]{'\0'};
        std::snprintf(buf, sizeof(buf), "latest Forward Distance: %.2f, period average distance: %.2f.\n", static_cast<double>(myDistanceToObstacleForward), accumulatedDistanceForward/logInterval_ticks);
        mySerial->write(buf);
        std::snprintf(buf, sizeof(buf), "latest Left Distance: %.2f, period average distance: %.2f.\n", static_cast<double>(myDistanceToObstacleLeft), accumulatedDistanceLeft/logInterval_ticks);
        mySerial->write(buf);
        std::snprintf(buf, sizeof(buf), "latest Right Distance: %.2f, period average distance: %.2f.\n", static_cast<double>(myDistanceToObstacleRight), accumulatedDistanceRight/logInterval_ticks);
        mySerial->write(buf);
        accumulatedDistanceForward = 0.0f;
        accumulatedDistanceLeft = 0.0f;
        accumulatedDistanceRight = 0.0f;
    }
}


void Logic::run(const std::atomic<bool>& stop) noexcept
{
    while (!stop.load())
    {
        getEnvironmentPicture();
        decideAction();
        executeAction();

        vTaskDelay(pdMS_TO_TICKS(tickPeriod_ms));
    }
    myMotor->stop(myPlannedAction.stopMode);
}

} // namespace app::logic
