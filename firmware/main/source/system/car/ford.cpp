#include "system/car/ford.h"

namespace app::car
{

Ford::Ford(driver::factory::Interface& factory) noexcept
{
    (void)factory;
}

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

} // namespace app::car
