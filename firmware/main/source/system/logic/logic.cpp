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
#include "system/runtime/output.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    constexpr std::uint32_t SerialBaudRate{115200U};
    constexpr driver::pwm::Config SteeringPwmConfig{9U, 300U};

    // Add, rename or disable application topics here. nullptr disables a path.
    constexpr app::communication::Topics CommunicationTopics{
        .publish = {
            .telemetry = "cnb/vagrant/telemetry",
            .configState = "cnb/vagrant/config/state",
            .commandState = "cnb/vagrant/command/state",
            .status = "cnb/vagrant/status",
        },
        .subscribe = {
            .configSet = "cnb/vagrant/config/set",
            .command = "cnb/vagrant/command",
        },
    };

    constexpr std::size_t bufLen{256U};
    // @brief the sleep period between two ticks. 50 ms -> 20 Hz
    constexpr int tickPeriod_ms{50};

    // @brief how often the state should be logged to serial
    constexpr int logInterval_ms{1000};
    constexpr int logInterval_ticks{logInterval_ms / tickPeriod_ms};
} // namespace

namespace app::logic
{
Logic::~Logic() noexcept
{
    deinitializeDrivers();
}

bool Logic::setDriverStyle(DriverStyle style) noexcept
{
    return myRuntimeControl.setDriverStyle(style);
}

Logic::Logic(driver::factory::Interface& factory) noexcept
    : myMotorForwardsPwm{factory.pwm(mp6550MotorPwmForwardPin)}
    , myMotorBackwardsPwm{factory.pwm(mp6550MotorPwmBackwardPin)}
    , myMotorSleep{factory.gpioOutput(mp6550MotorSleepPin)}
    , mySerial({factory.serial(SerialBaudRate)})
    , mySteeringServoPwm{factory.pwm(SteeringPwmConfig)}
    , myCommunication{factory, CommunicationTopics}
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

    if (mySteeringServoPwm) { mySteeringServo = factory.servo(*mySteeringServoPwm); }

    setStartState();
    if (!initializeDrivers())
    {
        if (mySerial && mySerial->isInitialized())
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
    myDistancesToObstacles.fill(std::numeric_limits<float>::quiet_NaN());
    myPlannedSpeed = 0.0F;
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

    myMotorSleep->write(false); // Keep the bridge disabled until runtime authorizes motion.

    if (!mySteeringServoPwm || !mySteeringServo || !mySteeringServo->init())
    {
        return false;
    }

    if (!myMotor->isInitialized() && !myMotor->init())
    {
        return false;
    }

    if (!myMotor->stop(driver::motor::StopMode::Coast))
    {
        return false;
    }

    return true;
}

void Logic::getEnvironmentPicture() noexcept
{
    for (std::size_t index{0U}; index < IrSensorCount; ++index)
    {
        if (myIrSensors[index] && myIrSensors[index]->isInitialized())
        {
            myDistancesToObstacles[index] = myIrSensors[index]->readDistance();
            // readDistance acquires voltage; inspect its raw sample without rereading ADC.
            myAdcRaw[index] = myIrSensorAdcs[index] ? myIrSensorAdcs[index]->lastRaw() : -1;
        }
        else
        {
            myDistancesToObstacles[index] = std::numeric_limits<float>::quiet_NaN();
            myAdcRaw[index] = -1;
        }
    }
}

void Logic::decideAction(std::uint32_t nowMs) noexcept
{
    const auto previousControlState = myRuntimeControl.controlState();
    const auto previousMotionState = myRuntimeControl.motionState();
    const auto previousReason = myRuntimeControl.stateReason();

    const auto style = myRuntimeControl.configuration().driverStyle;
    // Reapply only on a real change, otherwise GradualSweep restarts every tick.
    if (myNavigation.driverStyle() != style) { myNavigation.setDriverStyle(style); }
    const auto decision = myNavigation.decide(myDistancesToObstacles);
    mySteeringDegrees = decision.steeringDegrees;
    myPlannedSpeed = myRuntimeControl.evaluate(myDistancesToObstacles, nowMs,
                                               decision.pathSensor, decision.driveDuty);

    if ((previousControlState != myRuntimeControl.controlState())
        || (previousMotionState != myRuntimeControl.motionState())
        || (previousReason != myRuntimeControl.stateReason()))
    {
        myCommunication.notifyControlStateChanged();
    }
}

void Logic::executeAction() noexcept
{
    if (!myMotor || !mySteeringServo || !myMotorForwardsPwm || !myMotorBackwardsPwm || !myMotorSleep)
    {
        if (myMotorSleep) { myMotorSleep->write(false); }
        myRuntimeControl.forceDisarm(app::runtime::StateReason::ActuatorFault);
        myPlannedSpeed = 0.0F;
        myCommunication.notifyControlStateChanged();
        return;
    }
    if (!app::runtime::applyOutput(*myMotor, *mySteeringServo, *myMotorForwardsPwm,
                                  *myMotorBackwardsPwm, *myMotorSleep, myPlannedSpeed,
                                  mySteeringDegrees,
                                  myRuntimeControl.controlState() == app::runtime::ControlState::Armed))
    {
        myRuntimeControl.forceDisarm(app::runtime::StateReason::ActuatorFault);
        myPlannedSpeed = 0.0F;
        myCommunication.notifyControlStateChanged();
    }
}

void Logic::deinitializeDrivers() noexcept
{
    // Disable drive BEFORE any potentially blocking network shutdown.
    if (myMotorSleep) { myMotorSleep->write(false); }
    if (myMotorForwardsPwm) { myMotorForwardsPwm->setDuty(0.0F); }
    if (myMotorBackwardsPwm) { myMotorBackwardsPwm->setDuty(0.0F); }
    myCommunication.disconnect();
    if (mySteeringServo && mySteeringServo->isInitialized()) { mySteeringServo->deinit(); }
    if (myMotor && myMotor->isInitialized()) { myMotor->deinit(); }
    for (auto& adc : myIrSensorAdcs)
    {
        if (adc && adc->isInitialized()) { adc->deinit(); }
    }
    if (myMotorForwardsPwm && myMotorForwardsPwm->isInitialized()) { myMotorForwardsPwm->deinit(); }
    if (myMotorBackwardsPwm && myMotorBackwardsPwm->isInitialized()) { myMotorBackwardsPwm->deinit(); }
    if (mySteeringServoPwm && mySteeringServoPwm->isInitialized()) { mySteeringServoPwm->deinit(); }
    if (mySerial && mySerial->isInitialized()) { mySerial->disconnect(); }
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
            "IR cm L: %.2f (avg %.2f), C: %.2f (avg %.2f), R: %.2f (avg %.2f), ADC L/C/R: %d/%d/%d, steer: %.1f, speed: %.2f, armed: %s, main_stack_min: %u B\n",
            static_cast<double>(myDistancesToObstacles[0]), averages[0],
            static_cast<double>(myDistancesToObstacles[1]), averages[1],
            static_cast<double>(myDistancesToObstacles[2]), averages[2],
            static_cast<int>(myAdcRaw[0]), static_cast<int>(myAdcRaw[1]), static_cast<int>(myAdcRaw[2]),
            static_cast<double>(mySteeringDegrees), static_cast<double>(myPlannedSpeed),
            myRuntimeControl.controlState() == app::runtime::ControlState::Armed ? "yes" : "no",
            // ESP-IDF reports the minimum unused stack since task creation in bytes.
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

        if (mySerial && mySerial->isInitialized())
        {
            mySerial->write(buf);
        }

        accumulatedDistances.fill(0.0);
        validSamples.fill(0U);
        sampleCount = 0U;
    }
}

void Logic::publishTelemetry(std::uint32_t nowMs) noexcept
{
    app::communication::TelemetrySnapshot snapshot{};
    snapshot.distancesCm = myDistancesToObstacles;
    snapshot.adcRaw = myAdcRaw;
    snapshot.speedCommand = myPlannedSpeed;
    snapshot.steeringDegrees = mySteeringServo ? mySteeringServo->getDirection() : 0.0F;
    snapshot.forwardDuty = myMotorForwardsPwm ? myMotorForwardsPwm->duty() : 0.0F;
    snapshot.backwardDuty = myMotorBackwardsPwm ? myMotorBackwardsPwm->duty() : 0.0F;

    myCommunication.publishTelemetry(nowMs, snapshot, myRuntimeControl);
}

void Logic::run(const std::atomic<bool>& stop) noexcept
{
    TickType_t lastWakeTick{xTaskGetTickCount()};
    while (!stop.load())
    {
        const std::uint32_t nowMs = static_cast<std::uint32_t>(
            xTaskGetTickCount() * portTICK_PERIOD_MS);
        myCommunication.process(nowMs, myRuntimeControl);
        getEnvironmentPicture();
        decideAction(nowMs);
        executeAction();
        publishTelemetry(nowMs);
        logState();

        vTaskDelayUntil(&lastWakeTick, pdMS_TO_TICKS(tickPeriod_ms));
    }

    myRuntimeControl.forceDisarm(app::runtime::StateReason::OperatorStop);
    deinitializeDrivers();
}

} // namespace app::logic
