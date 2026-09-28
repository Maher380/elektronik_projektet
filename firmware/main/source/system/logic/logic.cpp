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
#include "driver/odometer/interface.h"
#include "driver/serial/interface.h"
#include "driver/servo/interface.h"
#include "driver/timer/interface.h"
#include "driver/wifi/interface.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr std::uint32_t DefaultPeriodMs{500U};
    constexpr std::uint32_t SerialBaudRate{115200U};
    constexpr const char *WifiSsid{CONFIG_CNB_WIFI_SSID};
    constexpr const char *WifiPassword{CONFIG_CNB_WIFI_PASSWORD};
    

    constexpr std::uint16_t bufLen{224U};
    // @brief the sleep period between two ticks. 50 ms -> 20 Hz 
    constexpr int tickPeriod_ms{50U};

    // @brief how often the state should be logged to serial
    constexpr int logInterval_ms{1000};
    constexpr int logInterval_ticks{logInterval_ms/tickPeriod_ms};

    // MQTT topics use the target car's key: cnb/<car>/...
    constexpr app::communication::Topics MqttTopics{
        {"cnb/" CONFIG_CNB_CAR_KEY "/telemetry", "cnb/" CONFIG_CNB_CAR_KEY "/config/state",
         "cnb/" CONFIG_CNB_CAR_KEY "/command/state", "cnb/" CONFIG_CNB_CAR_KEY "/status"},
        {"cnb/" CONFIG_CNB_CAR_KEY "/config/set", "cnb/" CONFIG_CNB_CAR_KEY "/command"},
    };

    // The car's own command help is written between these two parts.
    constexpr const char* CommandHelpTextStart{
        "Commands: SPEED <0-1>, FORWARD, BACKWARD, BRAKE, COAST, STOP, AUTO,\n"};
    constexpr const char* CommandHelpTextEnd{
        "  DRIVESTYLE <DECIDEACTION|SLOWLEFT|SLOWRIGHT|GRADUALSWEEP|MANUAL_BY_SERIAL>,\n"
        "  LOG <ON|OFF>, HELP\n"};

    void toUpperInPlace(char* text) noexcept
    {
        for (; *text != '\0'; ++text)
        {
            *text = static_cast<char>(std::toupper(static_cast<unsigned char>(*text)));
        }
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

void Logic::writeHelp() noexcept
{
    mySerial->write(CommandHelpTextStart);
    mySerial->write(myCar.helpText());
    mySerial->write(CommandHelpTextEnd);
}

void Logic::setDriverStyle(const DriverStyle style) noexcept
{
    myDriverStyle = style;
    if (style == DriverStyle::GradualSweep)
    {
        mySweepSteeringDegrees = -90.0F;
        mySweepDirection = 1.0F;
    }
}

Logic::Logic(driver::factory::Interface& factory, car::Interface& car) noexcept
    : myCar{car}
    , mySerial({factory.serial(SerialBaudRate)})
    , myCommunication{factory, MqttTopics}
{
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
    if (myCar.problem() != nullptr)
    {
        mySerial->write(myCar.problem());
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
        writeHelp();
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
    return myCar.init();
}

void Logic::deinitializeDrivers() noexcept
{
    myCar.deinit();
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
    navigation::Distances distances{};
    if (myCar.readObstacleDistances(distances))
    {
        myDistanceToObstacleForward = distances[navigation::Forward];
        myDistanceToObstacleLeft    = distances[navigation::Left];
        myDistanceToObstacleRight   = distances[navigation::Right];
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
        writeHelp();
    }
    else if (!myCar.handleSerialCommand(command, argument, parsed == 2, *mySerial))
    {
        mySerial->write("Unknown command. ");
        writeHelp();
    }

    if (isMotorCommand)
    {
        myMotorCommandPending = true;
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
    auto* const motor = myCar.motor();
    if (!motor || !motor->isInitialized())
    {
        return;
    }

    auto* const steering = myCar.steering();
    if (steering && steering->isInitialized())
    {
        steering->setDirection(myPlannedAction.steeringDegrees);
    }

    if (myPlannedAction.speed > 0.0F)
    {
        motor->setDirection(myPlannedAction.direction);
        motor->setSpeed(myPlannedAction.speed, myPlannedAction.stopMode);
    }
    else
    {
        motor->stop(myPlannedAction.stopMode);
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

        const auto* const odometer = myCar.odometer();
        const double odometerDistanceM{odometer ? static_cast<double>(odometer->distance()) : 0.0};
        const double odometerSpeedMps{odometer ? static_cast<double>(odometer->speed()) : 0.0};

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
            myCar.printMotorStatus(*mySerial);
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
        // Signed so a pass longer than tickPeriod_ms gives a negative remainder
        // instead of wrapping to a ~49 day delay.
        const auto elapsedMs{static_cast<std::int32_t>(afterLoopMs - nowMs)};
        const std::int32_t remainingMs{tickPeriod_ms - elapsedMs};
        vTaskDelay(pdMS_TO_TICKS(remainingMs > 0 ? remainingMs : 0));
    }
    if (auto* const motor = myCar.motor())
    {
        motor->stop(myPlannedAction.stopMode);
    }
    shutdownMqttOverlay();
}

} // namespace app::logic
