#include "system/car/ford.h"

#include "system/car/id.h"

namespace app::car
{

Ford::Ford(driver::factory::Interface& factory) noexcept
    : myIsOnCar{isRunningOn(factory, Id::Ford)}
{}

bool Ford::init() noexcept { return true; }

void Ford::deinit() noexcept {}

driver::motor::Interface* Ford::motor() noexcept { return nullptr; }

driver::servo::Interface* Ford::steering() noexcept { return nullptr; }

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
