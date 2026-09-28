#include "system/car/vagrant.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "driver/adc/interface.h"
#include "driver/factory/interface.h"
#include "driver/gpio/interface.h"
#include "driver/ir_sensor/interface.h"
#include "driver/motor/interface.h"
#include "driver/odometer/interface.h"
#include "driver/pwm/interface.h"
#include "driver/serial/interface.h"
#include "driver/servo/interface.h"
#include "system/car/id.h"
#include "system/communication/manager.h"

namespace
{
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
} // namespace

namespace app::car
{

Vagrant::Vagrant(driver::factory::Interface& factory) noexcept
    : myIsOnCar{isRunningOn(factory, Id::Vagrant)}
{
    if (!myIsOnCar) { return; }

    myMotorForwardsPwm = factory.pwm(mp6550MotorPwmForwardPin);
    myMotorBackwardsPwm = factory.pwm(mp6550MotorPwmBackwardPin);
    myMotorSleep = factory.gpioOutput(mp6550MotorSleepPin);
    myOdometerGpio = factory.gpioInputPullup(odometerPin);
    myIrSensorForwardAdc = factory.adc(IrSensorForwardAdcPin);
    myIrSensorLeftAdc = factory.adc(IrSensorLeftAdcPin);
    myIrSensorRightAdc = factory.adc(IrSensorRightAdcPin);
    mySteeringServoPwm = factory.pwm(driver::pwm::Config{
        .pin = steeringServoPwmPin,
        .frequencyHz = steeringServoPwmFrequencyHz,
    });

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
        mySteeringServo = factory.vagrantServo(*mySteeringServoPwm);
    }
    if (myOdometerGpio)
    {
        myOdometer = factory.odometer(*myOdometerGpio, driver::odometer::Config{
            .pulsesPerRevolution = odometerPulsesPerRevolution,
            .wheelDiameterM = odometerWheelDiameterM,
        });
    }
}

Vagrant::~Vagrant() noexcept = default;

bool Vagrant::init() noexcept
{
    // Nothing to initialize when the firmware runs on another car.
    if (!myIsOnCar) { return true; }

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
        !myOdometer )
    {
        return false;
    }

    // Initialize drivers that expose an explicit init operation.
    // The GPIO output is initialized by its constructor.
    // IR sensors become ready when their ADC dependencies are initialized.
    myMotorForwardsPwm->init();
    myMotorBackwardsPwm->init();
    myIrSensorForwardAdc->init();
    myIrSensorLeftAdc->init();
    myIrSensorRightAdc->init();
    myMotor->init();
    mySteeringServoPwm->init();
    mySteeringServo->init();
    myOdometer->init();

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
        !myOdometer->isInitialized())
    {
        return false;
    }

    myMotorSleep->write(true); // nSLEEP_HB HIGH keeps MP6550 awake.
    return true;
}

void Vagrant::deinit() noexcept
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
}

driver::motor::Interface* Vagrant::motor() noexcept { return myMotor.get(); }

driver::servo::Interface* Vagrant::steering() noexcept { return mySteeringServo.get(); }

driver::odometer::Interface* Vagrant::odometer() noexcept { return myOdometer.get(); }

bool Vagrant::readObstacleDistances(navigation::Distances& distances) noexcept
{
    //Check if all sensors are functional
    if (myIrSensorForward && myIrSensorForward->isInitialized() &&
        myIrSensorLeft    && myIrSensorLeft->isInitialized()    &&
        myIrSensorRight   && myIrSensorRight->isInitialized()   )
    {
        distances[navigation::Forward] = myIrSensorForward->readDistance();
        distances[navigation::Left]    = myIrSensorLeft->readDistance();
        distances[navigation::Right]   = myIrSensorRight->readDistance();
        return true;
    }
    return false;
}

const char* Vagrant::problem() const noexcept
{
    return myIsOnCar ? nullptr
                     : "Car ID pin (D10) says this is not Vagrant. Motor and steering are disabled.\n";
}

void Vagrant::enableMotorOutput() noexcept
{
    if (myMotorSleep) { myMotorSleep->write(true); }
}

void Vagrant::disableMotorOutput() noexcept
{
    // Remove motor power and put the driver to sleep.
    if (myMotorSleep) { myMotorSleep->write(false); }
    if (myMotorForwardsPwm) { myMotorForwardsPwm->setDuty(0.0F); }
    if (myMotorBackwardsPwm) { myMotorBackwardsPwm->setDuty(0.0F); }
}

bool Vagrant::handleSerialCommand(const char* command, const char* argument, const bool hasArgument,
                                  driver::serial::Interface& serial) noexcept
{
    if ((std::strcmp(command, "PWMDUTYFWD") == 0) && hasArgument)
    {
        if (trySetPwmDutyPercent(myMotorForwardsPwm.get(), argument, serial))
        {
            printPwmSettings(serial, "FWD", myMotorForwardsPwm.get());
        }
        return true;
    }
    if ((std::strcmp(command, "PWMDUTYBWD") == 0) && hasArgument)
    {
        if (trySetPwmDutyPercent(myMotorBackwardsPwm.get(), argument, serial))
        {
            printPwmSettings(serial, "BWD", myMotorBackwardsPwm.get());
        }
        return true;
    }
    return false;
}

const char* Vagrant::helpText() const noexcept
{
    return "  PWMDUTYFWD <0-100>, PWMDUTYBWD <0-100>,\n";
}

void Vagrant::printMotorStatus(driver::serial::Interface& serial) const noexcept
{
    printPwmSettings(serial, "FWD", myMotorForwardsPwm.get());
    printPwmSettings(serial, "BWD", myMotorBackwardsPwm.get());
}

void Vagrant::fillPartTelemetry(communication::TelemetrySnapshot& snapshot) const noexcept
{
    snapshot.adcRaw = {myIrSensorLeftAdc ? myIrSensorLeftAdc->lastRaw() : -1,
                       myIrSensorForwardAdc ? myIrSensorForwardAdc->lastRaw() : -1,
                       myIrSensorRightAdc ? myIrSensorRightAdc->lastRaw() : -1};
    snapshot.forwardDuty = myMotorForwardsPwm ? myMotorForwardsPwm->duty() : 0.0F;
    snapshot.backwardDuty = myMotorBackwardsPwm ? myMotorBackwardsPwm->duty() : 0.0F;
}

} // namespace app::car
