#include "system/car/ford.h"

#include "driver/factory/interface.h"
#include "driver/pwm/interface.h"
#include "driver/servo/interface.h"
#include "system/car/id.h"

namespace app::car
{

Ford::Ford(driver::factory::Interface& factory) noexcept
    : myIsOnCar{isRunningOn(factory, Id::Ford)}
{
    if (!myIsOnCar) { return; }

    mySteeringServoPwm = factory.pwm(driver::pwm::Config{
        .pin = steeringServoPwmPin,
        .frequencyHz = steeringServoPwmFrequencyHz,
    });

    if (mySteeringServoPwm)
    {
        mySteeringServo = factory.fordServo(*mySteeringServoPwm);
    }
}

Ford::~Ford() noexcept = default;

bool Ford::init() noexcept
{
    // Nothing to initialize when the firmware runs on another car.
    if (!myIsOnCar) { return true; }

    // Verify that all required driver objects were created.
    if (!mySteeringServoPwm || !mySteeringServo)
    {
        return false;
    }

    mySteeringServoPwm->init();
    mySteeringServo->init();

    // Verify that all required drivers are initialized and ready.
    return mySteeringServoPwm->isInitialized() && mySteeringServo->isInitialized();
}

void Ford::deinit() noexcept
{
    if (mySteeringServo && mySteeringServo->isInitialized())
    {
        mySteeringServo->deinit();
    }
    if (mySteeringServoPwm && mySteeringServoPwm->isInitialized())
    {
        mySteeringServoPwm->deinit();
    }
}

driver::motor::Interface* Ford::motor() noexcept { return nullptr; }

driver::servo::Interface* Ford::steering() noexcept { return mySteeringServo.get(); }

driver::odometer::Interface* Ford::odometer() noexcept { return nullptr; }

bool Ford::readObstacleDistances(navigation::Distances& distances) noexcept
{
    (void)distances;
    return false;
}

const char* Ford::problem() const noexcept
{
    return myIsOnCar ? nullptr
                     : "Car ID pin (D10) says this is not Ford. Motor and steering are disabled.\n";
}

} // namespace app::car
