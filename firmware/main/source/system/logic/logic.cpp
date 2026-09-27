#include <cstdio>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "esp_timer.h"

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
    

    constexpr std::uint16_t bufLen{224U};
    // @brief the sleep period between two ticks. 50 ms -> 20 Hz 
    constexpr int tickPeriod_ms{50U};

    // @brief how often the state should be logged to serial
    constexpr int logInterval_ms{1000};
    constexpr int logInterval_ticks{logInterval_ms/tickPeriod_ms};

    constexpr const char* CommandHelpText{
        "Commands: SPEED <0-1>, FORWARD, BACKWARD, BRAKE, COAST, STOP, AUTO,\n"
        "  PWMDUTYFWD <0-100>, PWMDUTYBWD <0-100>,\n"
        "  DRIVESTYLE <DECIDEACTION|SLOWLEFT|SLOWRIGHT|GRADUALSWEEP|MANUAL_BY_SERIAL>,\n"
        "  LOG <ON|OFF>, HELP\n"};

    void toUpperInPlace(char* text) noexcept
    {
        for (; *text != '\0'; ++text)
        {
            *text = static_cast<char>(std::toupper(static_cast<unsigned char>(*text)));
        }
    }

    bool trySetPwmDutyPercent(driver::pwm::Interface* pwm, const char* argument, driver::serial::Interface& serial) noexcept
    {
        float percent{0.0F};
        if (std::sscanf(argument, "%f", &percent) != 1)
        {
            serial.write("Usage: PWMDUTYFWD/PWMDUTYBWD <0-100>\n");
            return false;
        }

        const float duty{std::clamp(percent, 0.0F, 100.0F) / 100.0F};
        if ((pwm == nullptr) || !pwm->setDuty(duty))
        {
            serial.write("Failed to set PWM duty cycle\n");
            return false;
        }

        return true;
    }

    void printMotorSettings(driver::serial::Interface& serial, const app::logic::PlannedAction& action) noexcept
    {
        char buf[96]{'\0'};
        std::snprintf(buf, sizeof(buf), "Motor: direction=%s, speed=%.2f, stopMode=%s\n",
            action.direction == driver::motor::Direction::Forward ? "Forward" : "Backward",
            static_cast<double>(action.speed),
            action.stopMode == driver::motor::StopMode::Brake ? "Brake" : "Coast");
        serial.write(buf);
    }

    void printPwmSettings(driver::serial::Interface& serial, const char* label, const driver::pwm::Interface* pwm) noexcept
    {
        if (pwm == nullptr) { return; }

        char buf[96]{'\0'};
        std::snprintf(buf, sizeof(buf), "PWM %s: duty=%.1f%%, frequency=%luHz\n",
            label,
            static_cast<double>(pwm->duty() * 100.0F),
            static_cast<unsigned long>(pwm->frequencyHz()));
        serial.write(buf);
    }

    bool tryParseDriverStyle(const char* name, app::logic::DriverStyle& style) noexcept
    {
        if (std::strcmp(name, "DECIDEACTION") == 0) { style = app::logic::DriverStyle::DecideAction; return true; }
        if (std::strcmp(name, "SLOWLEFT") == 0) { style = app::logic::DriverStyle::SlowLeft; return true; }
        if (std::strcmp(name, "SLOWRIGHT") == 0) { style = app::logic::DriverStyle::SlowRight; return true; }
        if (std::strcmp(name, "GRADUALSWEEP") == 0) { style = app::logic::DriverStyle::GradualSweep; return true; }
        if (std::strcmp(name, "MANUAL_BY_SERIAL") == 0) { style = app::logic::DriverStyle::ManualBySerial; return true; }
        return false;
    }

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
    , myOdometerGpio{factory.gpioInputPullup(odometerPin)}
    , myIrSensorForwardAdc{factory.adc(IrSensorForwardAdcPin)}
    , myIrSensorLeftAdc{factory.adc(IrSensorLeftAdcPin)}
    , myIrSensorRightAdc{factory.adc(IrSensorRightAdcPin)}
    , mySerial({factory.serial(SerialBaudRate)})
    , mySteeringServoPwm{factory.pwm(SteeringPwmConfig)}
    , myCommunication{factory, MqttTopics}
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
    if (myOdometerGpio)
    {
        myOdometer = factory.odometer(*myOdometerGpio, driver::odometer::Config{
            .pulsesPerRevolution = odometerPulsesPerRevolution,
            .wheelDiameterM = odometerWheelDiameterM,
        });
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
    // MQTT overlay: boot disarmed after SCRUM-16 driver initialization.
    initializeMqttOverlay();
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
        mySerial->write(CommandHelpText);
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
        !mySerial ||
        !myOdometer )
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
    myOdometer->init();
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
        !myOdometer->isInitialized() ||
        !mySerial->isInitialized())
    {
        return false;
    }

    myMotorSleep->write(true); // nSLEEP_HB HIGH keeps MP6550 awake.

    return true;
}

void Logic::deinitializeDrivers() noexcept
{
    if (myOdometer && myOdometer->isInitialized())
    {
        myOdometer->deinit();
    }
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
    case DriverStyle::ManualBySerial:
        // myPlannedAction was already updated by processSerialCommand().
        break;
    }
}

void Logic::processSerialCommand() noexcept
{
    if (!mySerial || !mySerial->isInitialized() || !mySerial->isDataAvailable())
    {
        return;
    }

    char line[32]{'\0'};
    if (mySerial->read(line, sizeof(line)) == 0U)
    {
        return;
    }

    char command[16]{'\0'};
    char argument[20]{'\0'};
    const int parsed = std::sscanf(line, "%15s %19s", command, argument);
    if (parsed < 1)
    {
        return;
    }

    toUpperInPlace(command);
    toUpperInPlace(argument);

    const bool isHelp = std::strcmp(command, "HELP") == 0;
    const bool isDriveStyle = std::strcmp(command, "DRIVESTYLE") == 0;
    const bool isLog = std::strcmp(command, "LOG") == 0;

    if (!isHelp && !isDriveStyle && !isLog && (myDriverStyle != DriverStyle::ManualBySerial))
    {
        mySerial->write("Ignored: switch to manual mode first with DRIVESTYLE MANUAL_BY_SERIAL\n");
        return;
    }

    bool isMotorCommand{false};
    bool isPwmFwdCommand{false};
    bool isPwmBwdCommand{false};

    float speed{0.0F};
    if ((std::strcmp(command, "SPEED") == 0) && (parsed == 2) && (std::sscanf(argument, "%f", &speed) == 1))
    {
        myPlannedAction.speed = std::clamp(speed, 0.0F, 1.0F);
        isMotorCommand = true;
    }
    else if (std::strcmp(command, "FORWARD") == 0)
    {
        myPlannedAction.direction = driver::motor::Direction::Forward;
        isMotorCommand = true;
    }
    else if (std::strcmp(command, "BACKWARD") == 0)
    {
        myPlannedAction.direction = driver::motor::Direction::Backward;
        isMotorCommand = true;
    }
    else if (std::strcmp(command, "BRAKE") == 0)
    {
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
        isMotorCommand = true;
    }
    else if (std::strcmp(command, "COAST") == 0)
    {
        myPlannedAction.stopMode = driver::motor::StopMode::Coast;
        isMotorCommand = true;
    }
    else if (std::strcmp(command, "STOP") == 0)
    {
        myPlannedAction.speed = 0.0F;
        isMotorCommand = true;
    }
    else if (std::strcmp(command, "AUTO") == 0)
    {
        setDriverStyle(DriverStyle::DecideAction);
    }
    else if ((std::strcmp(command, "PWMDUTYFWD") == 0) && (parsed == 2))
    {
        isPwmFwdCommand = trySetPwmDutyPercent(myMotorForwardsPwm.get(), argument, *mySerial);
    }
    else if ((std::strcmp(command, "PWMDUTYBWD") == 0) && (parsed == 2))
    {
        isPwmBwdCommand = trySetPwmDutyPercent(myMotorBackwardsPwm.get(), argument, *mySerial);
    }
    else if (isDriveStyle && (parsed == 2))
    {
        DriverStyle style{};
        if (tryParseDriverStyle(argument, style))
        {
            setDriverStyle(style);
        }
        else
        {
            mySerial->write("Unknown drive style. Use DECIDEACTION, SLOWLEFT, SLOWRIGHT, GRADUALSWEEP, MANUAL_BY_SERIAL\n");
        }
    }
    else if (isLog && (parsed == 2))
    {
        if (std::strcmp(argument, "ON") == 0)
        {
            myLogEnabled = true;
            mySerial->write("Logging enabled\n");
        }
        else if (std::strcmp(argument, "OFF") == 0)
        {
            myLogEnabled = false;
            mySerial->write("Logging disabled\n");
        }
        else
        {
            mySerial->write("Usage: LOG <ON|OFF>\n");
        }
    }
    else if (isHelp)
    {
        mySerial->write(CommandHelpText);
    }
    else
    {
        mySerial->write("Unknown command. ");
        mySerial->write(CommandHelpText);
    }

    if (isMotorCommand)
    {
        myMotorCommandPending = true;
    }
    if (isPwmFwdCommand)
    {
        printPwmSettings(*mySerial, "FWD", myMotorForwardsPwm.get());
    }
    if (isPwmBwdCommand)
    {
        printPwmSettings(*mySerial, "BWD", myMotorBackwardsPwm.get());
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
    if (myDistanceToObstacleForward > std::max(myDistanceToObstacleLeft, myDistanceToObstacleRight))
    {
        myPlannedAction.steeringDegrees = 0.0F;
        distanceToClosestObject = myDistanceToObstacleForward;
    }
    else if (myDistanceToObstacleLeft > myDistanceToObstacleRight)
    {
        myPlannedAction.steeringDegrees = -90.0F;
        distanceToClosestObject = myDistanceToObstacleLeft;
    }
    else
    {
        myPlannedAction.steeringDegrees = 90.0F;
        distanceToClosestObject = myDistanceToObstacleRight;
    }

    if (distanceToClosestObject < myStopDistanceCm)
    {
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
    }
    else
    {
        myPlannedAction.speed = myDriveDuty;
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
    if (std::min({myDistanceToObstacleForward, myDistanceToObstacleLeft, myDistanceToObstacleRight}) < myStopDistanceCm)
    {
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
    }
    else
    {
        myPlannedAction.speed = myDriveDuty;
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
    if (std::min({myDistanceToObstacleForward, myDistanceToObstacleLeft, myDistanceToObstacleRight}) < myStopDistanceCm)
    {
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
    }
    else
    {
        myPlannedAction.speed = myDriveDuty;
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
    if (std::min({myDistanceToObstacleForward, myDistanceToObstacleLeft, myDistanceToObstacleRight}) < myStopDistanceCm)
    {
        myPlannedAction.speed = 0.0F;
        myPlannedAction.stopMode = driver::motor::StopMode::Brake;
    }
    else
    {
        myPlannedAction.speed = myDriveDuty;
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
        myMotor->setDirection(myPlannedAction.direction);
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

        const double odometerDistanceM{myOdometer ? static_cast<double>(myOdometer->distance()) : 0.0};
        const double odometerSpeedMps{myOdometer ? static_cast<double>(myOdometer->speed()) : 0.0};

        std::snprintf(
            buf,
            sizeof(buf),
            "IR cm L: %.2f (avg %.2f), C: %.2f (avg %.2f), R: %.2f (avg %.2f), "
            "Steering: %.1f deg, Speed: %.2f, Motor: %s, Brake mode: %s, "
            "Odometer: %.2f m, %.2f m/s\n",
            static_cast<double>(myDistanceToObstacleLeft), averageLeft,
            static_cast<double>(myDistanceToObstacleForward), averageForward,
            static_cast<double>(myDistanceToObstacleRight), averageRight,
            static_cast<double>(myPlannedAction.steeringDegrees),
            static_cast<double>(myPlannedAction.speed),
            myPlannedAction.direction == driver::motor::Direction::Forward ? "Forward" : "Backward",
            myPlannedAction.stopMode == driver::motor::StopMode::Brake ? "Brake" : "Coast",
            odometerDistanceM, odometerSpeedMps);

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
        const auto nowMs = static_cast<std::uint32_t>(
            xTaskGetTickCount() * portTICK_PERIOD_MS);

        processSerialCommand();
        getEnvironmentPicture();
        decideAction();
        // Only MQTT authorization surrounds the unchanged executeAction().
        if (authorizeMqttAction(nowMs))
        {
            executeAction();
        }
        if (myMotorCommandPending)
        {
            printMotorSettings(*mySerial, myPlannedAction);
            printPwmSettings(*mySerial, "FWD", myMotorForwardsPwm.get());
            printPwmSettings(*mySerial, "BWD", myMotorBackwardsPwm.get());
            myMotorCommandPending = false;
        }
        if (myLogEnabled)
        {
            logState();
        }
        processMqttOverlay(nowMs);

        publishMqttTelemetry(nowMs);

        const auto afterLoopMs = static_cast<std::uint32_t>(
                        xTaskGetTickCount() * portTICK_PERIOD_MS);
        const auto elapsedMs{(afterLoopMs - nowMs)};
        const auto  remainingMs{static_cast<std::int32_t>(tickPeriod_ms) - elapsedMs};
        vTaskDelay(pdMS_TO_TICKS(remainingMs > 0 ? remainingMs : 0));
    }
    myMotor->stop(myPlannedAction.stopMode);
    shutdownMqttOverlay();
}

} // namespace app::logic
