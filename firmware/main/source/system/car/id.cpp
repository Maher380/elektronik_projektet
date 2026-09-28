#include "system/car/id.h"

#include "driver/factory/interface.h"
#include "driver/gpio/interface.h"

namespace app::car
{

bool readHardwareCar(driver::factory::Interface& factory, Id& car) noexcept
{
    const auto idPin = factory.gpioInputPullup(IdPin);
    if (!idPin || !idPin->isInitialized()) { return false; }

    car = isIdPinLevelOf(Id::Vagrant, idPin->read()) ? Id::Vagrant : Id::Ford;
    return true;
}

bool isRunningOn(driver::factory::Interface& factory, const Id car) noexcept
{
    Id hardwareCar{};
    return readHardwareCar(factory, hardwareCar) && (hardwareCar == car);
}

} // namespace app::car
