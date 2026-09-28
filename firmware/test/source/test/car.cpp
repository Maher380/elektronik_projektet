#include <cstdio>

#include "driver/factory/stub.h"
#include "system/car/id.h"
#include "test/car.h"

namespace
{
bool expect(bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("Car test failed: %s\n", message);
        return false;
    }

    return true;
}
} // namespace

namespace test
{
bool runCarTest() noexcept
{
    using app::car::Id;

    // Car ID pin: Vagrant leaves it open (high), Ford ties it to GND (low).
    bool passed = expect(app::car::isIdPinLevelOf(Id::Vagrant, true), "high pin should be Vagrant")
        && expect(!app::car::isIdPinLevelOf(Id::Vagrant, false), "low pin should not be Vagrant")
        && expect(app::car::isIdPinLevelOf(Id::Ford, false), "low pin should be Ford")
        && expect(!app::car::isIdPinLevelOf(Id::Ford, true), "high pin should not be Ford");

    // The stub GPIO reads low, like a Ford car ID pin.
    driver::factory::Stub factory;
    passed = passed
        && expect(app::car::isRunningOn(factory, Id::Ford), "stub pin should read as Ford")
        && expect(!app::car::isRunningOn(factory, Id::Vagrant), "stub pin should not read as Vagrant");

    return passed;
}
} // namespace test
