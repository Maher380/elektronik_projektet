#include <cstdio>

#include <algorithm>
#include <cmath>
#include <cstddef>
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
    constexpr std::uint8_t AdcPin{1U};
    constexpr std::uint8_t LedPin{6U};
    constexpr std::uint32_t DefaultPeriodMs{500U};
    constexpr std::uint32_t SerialBaudRate{115200U};
    constexpr driver::pwm::Config SteeringPwmConfig{
        .pin = 9U,
        .frequencyHz = 300U,
    };
    constexpr const char *WifiSsid{CONFIG_CNB_WIFI_SSID};
    constexpr const char *WifiPassword{CONFIG_CNB_WIFI_PASSWORD};
    

    constexpr std::uint8_t bufLen{192U};
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
    if (style == DriverStyle::GradualSweep)
    {
        mySweepSteeringDegrees = -90.0F;
        mySweepDirection = 1.0F;
    }
}

Logic::Logic(driver::factory::Interface& factory) noexcept
    : myMotorForwardsPwm{factory.pwm(mp6550MotorPwmForwardPin)}
    , myMotorBackwardsPwm{factory.pwm(mp6550MotorPwmBackwardPin)}
    , myMotorSleep{factory.gpioOutput(mp6550MotorSleepPin)}
    , myIrSensorForwardAdc{factory.adc(IrSensorForwardAdcPin)}
    , myIrSensorLeftAdc{factory.adc(IrSensorLeftAdcPin)}
    , myIrSensorRightAdc{factory.adc(IrSensorRightAdcPin)}
    , mySerial({factory.serial(SerialBaudRate)})
    , mySteeringServoPwm{factory.pwm(SteeringPwmConfig)}
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
        if (mySerial)
        {
            mySerial->write("Initialization failed!\n");
        }

        deinitializeDrivers();

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
    mySweepSteeringDegrees = -90.0F;
    mySweepDirection = 1.0F;

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

void Logic::deinitializeDrivers() noexcept
{
    if (mySteeringServo && mySteeringServo->isInitialized())
    {
        mySteeringServo->deinit();
    }
    if (myMotor && myMotor->isInitialized())
    {
        myMotor->deinit();
    }
    if (myIrSensorForwardAdc && myIrSensorForwardAdc->isInitialized())
    {
        myIrSensorForwardAdc->deinit();
    }
    if (myIrSensorLeftAdc && myIrSensorLeftAdc->isInitialized())
    {
        myIrSensorLeftAdc->deinit();
    }
    if (myIrSensorRightAdc && myIrSensorRightAdc->isInitialized())
    {
        myIrSensorRightAdc->deinit();
    }
    if (myMotorForwardsPwm && myMotorForwardsPwm->isInitialized())
    {
        myMotorForwardsPwm->deinit();
    }
    if (myMotorBackwardsPwm && myMotorBackwardsPwm->isInitialized())
    {
        myMotorBackwardsPwm->deinit();
    }
    if (mySteeringServoPwm && mySteeringServoPwm->isInitialized())
    {
        mySteeringServoPwm->deinit();
    }
    if (mySerial && mySerial->isInitialized())
    {
        mySerial->disconnect();
    }
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

    const auto invalidDistance = std::numeric_limits<float>::quiet_NaN();
    myDistanceToObstacleForward = invalidDistance;
    myDistanceToObstacleLeft = invalidDistance;
    myDistanceToObstacleRight = invalidDistance;
}

bool Logic::hasValidEnvironmentPicture() const noexcept
{
    return std::isfinite(myDistanceToObstacleForward) &&
           std::isfinite(myDistanceToObstacleLeft) &&
           std::isfinite(myDistanceToObstacleRight);
}

void Logic::decideAction() noexcept
{
    switch (myDriverStyle)
    {
    case DriverStyle::DecideAction:
        decideNormalAction();
        break;
    case DriverStyle::GradualSweep:
        decideGradualSweepAction();
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
    if (!hasValidEnvironmentPicture())
    {
        myPlannedAction.steeringDegrees = 0.0F;
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
        return;
    }

    float distanceToClosestObject;
    if (myDistanceToObstacleForward > std::min(myDistanceToObstacleLeft, myDistanceToObstacleRight))
    {
        myPlannedAction.steeringDegrees = 0.0F;
        distanceToClosestObject = myDistanceToObstacleForward;
    }
    else if (myDistanceToObstacleLeft > myDistanceToObstacleRight)
    {
        myPlannedAction.steeringDegrees = 90.0F;
        distanceToClosestObject = myDistanceToObstacleLeft;
    }
    else
    {
        myPlannedAction.steeringDegrees = 90.0F;
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

void Logic::decideGradualSweepAction() noexcept
{
    constexpr float SweepStepDegrees{5.0F};

    if (!hasValidEnvironmentPicture())
    {
        myPlannedAction.steeringDegrees = 0.0F;
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
        return;
    }

    myPlannedAction.steeringDegrees = mySweepSteeringDegrees;
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

    if (mySweepSteeringDegrees >= 90.0F)
    {
        mySweepDirection = -1.0F;
    }
    else if (mySweepSteeringDegrees <= -90.0F)
    {
        mySweepDirection = 1.0F;
    }
    mySweepSteeringDegrees += mySweepDirection * SweepStepDegrees;
}

void Logic::decideSlowLeftAction() noexcept
{
    if (!hasValidEnvironmentPicture())
    {
        myPlannedAction.steeringDegrees = 0.0F;
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
        return;
    }

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
    if (!hasValidEnvironmentPicture())
    {
        myPlannedAction.steeringDegrees = 0.0F;
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
        return;
    }

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
    static double accumulatedDistanceForward{0.0};
    static double accumulatedDistanceLeft{0.0};
    static double accumulatedDistanceRight{0.0};
    static std::size_t validSamplesForward{0U};
    static std::size_t validSamplesLeft{0U};
    static std::size_t validSamplesRight{0U};
    static std::size_t sampleCount{0U};

    if (std::isfinite(myDistanceToObstacleForward))
    {
        accumulatedDistanceForward += myDistanceToObstacleForward;
        ++validSamplesForward;
    }
    if (std::isfinite(myDistanceToObstacleLeft))
    {
        accumulatedDistanceLeft += myDistanceToObstacleLeft;
        ++validSamplesLeft;
    }
    if (std::isfinite(myDistanceToObstacleRight))
    {
        accumulatedDistanceRight += myDistanceToObstacleRight;
        ++validSamplesRight;
    }

    ++sampleCount;
    if (sampleCount >= static_cast<std::size_t>(logInterval_ticks))
    {
        char buf[bufLen]{'\0'};
        const double averageLeft = validSamplesLeft > 0U
            ? accumulatedDistanceLeft / static_cast<double>(validSamplesLeft)
            : std::numeric_limits<double>::quiet_NaN();
        const double averageForward = validSamplesForward > 0U
            ? accumulatedDistanceForward / static_cast<double>(validSamplesForward)
            : std::numeric_limits<double>::quiet_NaN();
        const double averageRight = validSamplesRight > 0U
            ? accumulatedDistanceRight / static_cast<double>(validSamplesRight)
            : std::numeric_limits<double>::quiet_NaN();

        std::snprintf(
            buf,
            sizeof(buf),
            "IR cm L: %.2f (avg %.2f), C: %.2f (avg %.2f), R: %.2f (avg %.2f), "
            "Direction: %.1f deg, Speed: %.2f, Brake mode: %s\n",
            static_cast<double>(myDistanceToObstacleLeft), averageLeft,
            static_cast<double>(myDistanceToObstacleForward), averageForward,
            static_cast<double>(myDistanceToObstacleRight), averageRight,
            static_cast<double>(myPlannedAction.steeringDegrees),
            static_cast<double>(myPlannedAction.speed),
            myPlannedAction.stopMode == driver::motor::StopMode::Brake ? "Brake" : "Coast");

        if (mySerial && mySerial->isInitialized())
        {
            mySerial->write(buf);
        }

        accumulatedDistanceForward = 0.0;
        accumulatedDistanceLeft = 0.0;
        accumulatedDistanceRight = 0.0;
        validSamplesForward = 0U;
        validSamplesLeft = 0U;
        validSamplesRight = 0U;
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
    myMotor->stop(myPlannedAction.stopMode);
}

} // namespace app::logic
