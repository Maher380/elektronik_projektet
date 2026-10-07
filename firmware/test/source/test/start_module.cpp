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

using driver::start_module::State;

constexpr std::uint32_t HoldMs{20U};

bool isWaiting(const driver::start_module::Interface& module) noexcept { return State::Waiting == module.state(); }
bool isStarted(const driver::start_module::Interface& module) noexcept { return State::Started == module.state(); }
bool isStopped(const driver::start_module::Interface& module) noexcept { return State::Stopped == module.state(); }

/** Low, then high held for the hold time, starts the module. */
bool startsAfterHold() noexcept
{
    driver::gpio::Stub input;
    driver::start_module::Gpio module{input, HoldMs};

    module.update(0U);
    input.write(true);
    module.update(10U);
    const bool waitingDuringHold{isWaiting(module)};
    module.update(10U + HoldMs - 1U);
    const bool waitingJustBeforeHold{isWaiting(module)};
    module.update(10U + HoldMs);

    return expect(module.isInitialized(), "module should be ready with its GPIO")
        && expect(waitingDuringHold, "start should not count before the hold time")
        && expect(waitingJustBeforeHold, "start should not count 1 ms before the hold time")
        && expect(isStarted(module), "start should count once the hold time has passed");
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
    const bool spikeIgnored{isWaiting(module)};
    module.update(15U + HoldMs);

    return expect(spikeIgnored, "a short spike should not start the module")
        && expect(isStarted(module), "a full hold after the spike should start the module");
}

/** High at power-on needs a low and a new high before it counts. */
bool needsLowFirst() noexcept
{
    driver::gpio::Stub input;
    input.write(true);
    driver::start_module::Gpio module{input, HoldMs};

    module.update(0U);
    module.update(1000U);
    const bool highAtBootIgnored{isWaiting(module)};
    input.write(false);
    module.update(1010U);
    input.write(true);
    module.update(1020U);
    module.update(1020U + HoldMs);

    return expect(highAtBootIgnored, "high at power-on should not start the module")
        && expect(isStarted(module), "low then high after power-on should start the module");
}

/** The first low after the start stops the module at once, with no hold time. */
bool stopsOnFirstLow() noexcept
{
    driver::gpio::Stub input;
    driver::start_module::Gpio module{input, 0U};

    module.update(0U);
    input.write(true);
    module.update(0U);
    const bool startedAtOnce{isStarted(module)};
    module.update(100U);
    const bool staysStartedWhileHigh{isStarted(module)};
    input.write(false);
    module.update(101U);

    return expect(startedAtOnce, "a hold time of 0 should start on the first high after a low")
        && expect(staysStartedWhileHigh, "the module should stay started while the signal is high")
        && expect(isStopped(module), "the first low after the start should stop the module");
}

/** Stopped is final: a new start signal does not start the module again. */
bool stoppedIsFinal() noexcept
{
    driver::gpio::Stub input;
    driver::start_module::Gpio module{input, HoldMs};

    module.update(0U);
    input.write(true);
    module.update(0U);
    module.update(HoldMs);
    input.write(false);
    module.update(HoldMs + 1U);
    input.write(true);
    module.update(HoldMs + 2U);
    module.update(HoldMs * 10U);

    return expect(isStopped(module), "a high after the stop should not start the module again");
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
    const bool waitingAcrossWrap{isWaiting(module)};
    module.update(nearWrap + HoldMs);

    return expect(waitingAcrossWrap, "wrap-around should not end the hold time early")
        && expect(isStarted(module), "start should count after the hold time across the wrap");
}

bool factoryStub() noexcept
{
    driver::factory::Stub factory;
    driver::gpio::Stub input;
    auto module = factory.startModule(input, HoldMs);

    return expect(nullptr != module, "factory should create a start module")
        && expect(module->isInitialized() && isWaiting(*module), "factory stub should wait for a start");
}
} // namespace

namespace test
{
bool runStartModuleTest() noexcept
{
    return startsAfterHold()
        && ignoresSpikes()
        && needsLowFirst()
        && stopsOnFirstLow()
        && stoppedIsFinal()
        && handlesClockWrap()
        && factoryStub();
}
} // namespace test
