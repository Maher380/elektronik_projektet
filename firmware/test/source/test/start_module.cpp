#include <cstdint>
#include <cstdio>

#include "driver/factory/stub.h"
#include "driver/gpio/stub.h"
#include "driver/start_module/gpio.h"
#include "test/start_module.h"

namespace
{
bool expect(bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("Start module test failed: %s\n", message);
        return false;
    }

    return true;
}

constexpr std::uint32_t HoldMs{20U};

/** Low, then high held for the hold time, starts the module. */
bool startsAfterHold() noexcept
{
    driver::gpio::Stub input;
    driver::start_module::Gpio module{input, HoldMs};

    module.update(0U);
    input.write(true);
    module.update(10U);
    const bool waitingDuringHold{!module.isStarted()};
    module.update(10U + HoldMs - 1U);
    const bool waitingJustBeforeHold{!module.isStarted()};
    module.update(10U + HoldMs);

    return expect(module.isInitialized(), "module should be ready with its GPIO")
        && expect(waitingDuringHold, "start should not count before the hold time")
        && expect(waitingJustBeforeHold, "start should not count 1 ms before the hold time")
        && expect(module.isStarted(), "start should count once the hold time has passed");
}

/** A spike shorter than the hold time is ignored, and the hold time restarts. */
bool ignoresSpikes() noexcept
{
    driver::gpio::Stub input;
    driver::start_module::Gpio module{input, HoldMs};

    module.update(0U);
    input.write(true);
    module.update(5U);
    input.write(false);
    module.update(10U);
    input.write(true);
    module.update(15U);
    module.update(5U + HoldMs);
    const bool spikeIgnored{!module.isStarted()};
    module.update(15U + HoldMs);

    return expect(spikeIgnored, "a short spike should not start the module")
        && expect(module.isStarted(), "a full hold after the spike should start the module");
}

/** High at power-on needs a low and a new high before it counts. */
bool needsLowFirst() noexcept
{
    driver::gpio::Stub input;
    input.write(true);
    driver::start_module::Gpio module{input, HoldMs};

    module.update(0U);
    module.update(1000U);
    const bool highAtBootIgnored{!module.isStarted()};
    input.write(false);
    module.update(1010U);
    input.write(true);
    module.update(1020U);
    module.update(1020U + HoldMs);

    return expect(highAtBootIgnored, "high at power-on should not start the module")
        && expect(module.isStarted(), "low then high after power-on should start the module");
}

/** Once started, the module stays started whatever the signal does. */
bool staysStarted() noexcept
{
    driver::gpio::Stub input;
    driver::start_module::Gpio module{input, 0U};

    module.update(0U);
    input.write(true);
    module.update(0U);
    const bool startedAtOnce{module.isStarted()};
    input.write(false);
    module.update(100U);

    return expect(startedAtOnce, "a hold time of 0 should start on the first high after a low")
        && expect(module.isStarted(), "a low after the start should not take the start back");
}

/** The hold time is measured right when the millisecond clock wraps around. */
bool handlesClockWrap() noexcept
{
    driver::gpio::Stub input;
    driver::start_module::Gpio module{input, HoldMs};

    constexpr std::uint32_t nearWrap{0xFFFFFFFFU - 5U};
    module.update(nearWrap);
    input.write(true);
    module.update(nearWrap);
    module.update(nearWrap + 10U);
    const bool waitingAcrossWrap{!module.isStarted()};
    module.update(nearWrap + HoldMs);

    return expect(waitingAcrossWrap, "wrap-around should not end the hold time early")
        && expect(module.isStarted(), "start should count after the hold time across the wrap");
}

bool factoryStub() noexcept
{
    driver::factory::Stub factory;
    driver::gpio::Stub input;
    auto module = factory.startModule(input, HoldMs);

    return expect(nullptr != module, "factory should create a start module")
        && expect(module->isInitialized() && !module->isStarted(), "factory stub should wait for a start");
}
} // namespace

namespace test
{
bool runStartModuleTest() noexcept
{
    return startsAfterHold()
        && ignoresSpikes()
        && needsLowFirst()
        && staysStarted()
        && handlesClockWrap()
        && factoryStub();
}
} // namespace test
