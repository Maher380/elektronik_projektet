#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "driver/distance_sensor/hcsr04_qwiic.h"
#include "driver/i2c/interface.h"
#include "test/hcsr04_qwiic.h"

namespace
{
using driver::distance_sensor::HcSr04Qwiic;
namespace reg = driver::distance_sensor::hcsr04_qwiic;

bool expect(bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("HC-SR04 Qwiic test failed: %s\n", message);
        return false;
    }

    return true;
}

bool isNear(float value, float expected) noexcept
{
    return std::fabs(value - expected) < 0.01F;
}

/**
 * @brief Several simulated Qwiic adapters on one bus.
 *
 * driver::i2c::Stub holds one device whose registers are big endian, and these are three
 * devices whose readings are little endian, so this is its own stub rather than a change to
 * the shared one that the A89301 tests rely on.
 */
class FakeBus final : public driver::i2c::Interface
{
public:
    /** One simulated sensor. */
    struct Sensor
    {
        std::uint8_t address{0U};
        std::uint16_t distanceCm{0U};
        bool answering{true};
        std::uint32_t triggers{0U};
    };

    bool init() noexcept override
    {
        myInitialized = true;
        return true;
    }

    bool deinit() noexcept override
    {
        myInitialized = false;
        return true;
    }

    bool isInitialized() const noexcept override
    {
        return myInitialized;
    }

    bool probe(const std::uint8_t address) noexcept override
    {
        return reachable(find(address));
    }

    bool write(const std::uint8_t address, const std::uint8_t* data,
               const std::size_t length) noexcept override
    {
        Sensor* const sensor{find(address)};
        if (!reachable(sensor) || (data == nullptr) || (length != 1U)) { return false; }

        myPointer = data[0];
        if (myPointer == reg::TriggerRegister) { ++sensor->triggers; }
        return true;
    }

    bool read(const std::uint8_t address, std::uint8_t* data,
              const std::size_t length) noexcept override
    {
        const Sensor* const sensor{find(address)};
        if (!reachable(sensor) || (data == nullptr) || (length != 2U)) { return false; }

        // Little endian, as the board reports it.
        data[0] = static_cast<std::uint8_t>(sensor->distanceCm & 0xFFU);
        data[1] = static_cast<std::uint8_t>(sensor->distanceCm >> 8U);
        return true;
    }

    bool writeRead(const std::uint8_t address, const std::uint8_t* writeData,
                   const std::size_t writeLength, std::uint8_t* readData,
                   const std::size_t readLength) noexcept override
    {
        if (!write(address, writeData, writeLength)) { return false; }
        return read(address, readData, readLength);
    }

    /** Add a simulated sensor at an address. */
    void add(const std::uint8_t address, const std::uint16_t distanceCm) noexcept
    {
        if (myCount >= MaxSensors) { return; }
        mySensors[myCount] = Sensor{address, distanceCm, true, 0U};
        ++myCount;
    }

    /** The simulated sensor at an address, or nullptr if nothing is there. */
    Sensor* find(const std::uint8_t address) noexcept
    {
        for (std::size_t i{0U}; i < myCount; ++i)
        {
            if (mySensors[i].address == address) { return &mySensors[i]; }
        }
        return nullptr;
    }

    const Sensor* find(const std::uint8_t address) const noexcept
    {
        for (std::size_t i{0U}; i < myCount; ++i)
        {
            if (mySensors[i].address == address) { return &mySensors[i]; }
        }
        return nullptr;
    }

private:
    static constexpr std::size_t MaxSensors{4U};

    bool reachable(const Sensor* const sensor) const noexcept
    {
        return myInitialized && (sensor != nullptr) && sensor->answering;
    }

    Sensor mySensors[MaxSensors]{};
    std::size_t myCount{0U};
    std::uint8_t myPointer{0U};
    bool myInitialized{false};
};
} // namespace

namespace test
{
bool runHcSr04QwiicTest() noexcept
{
    FakeBus bus;
    (void)bus.init();
    bus.add(reg::FirstAddress, 57U);

    // A sensor that is on the bus comes up; one that is not stays down and pings nothing.
    HcSr04Qwiic sensor{bus, reg::FirstAddress};
    HcSr04Qwiic missing{bus, static_cast<std::uint8_t>(reg::FirstAddress + 1U)};
    bool passed = expect(sensor.isInitialized(), "a sensor that acknowledges should come up")
        && expect(!missing.isInitialized(), "a sensor that does not acknowledge should stay down")
        && expect(std::isnan(sensor.readDistance()), "a sensor should read NaN before it pings")
        && expect(!missing.trigger(0U), "a sensor that is not there should refuse to trigger")
        && expect(sensor.address() == reg::FirstAddress, "the address should be kept");

    // An address outside the board's eight is refused without touching the bus.
    HcSr04Qwiic outOfRange{bus, static_cast<std::uint8_t>(reg::LastAddress + 1U)};
    passed = passed && expect(!outOfRange.isInitialized(), "an address above 0x37 should be refused");

    // A measurement is not readable until it has had MeasurementMs to come back.
    passed = passed
        && expect(sensor.trigger(1000U), "triggering a present sensor should work")
        && expect(!sensor.isResultDue(1000U + reg::MeasurementMs - 1U),
                  "a result should not be due early")
        && expect(!sensor.poll(1000U + reg::MeasurementMs - 1U), "an early poll should do nothing")
        && expect(std::isnan(sensor.readDistance()), "an early poll should leave no reading")
        && expect(sensor.isResultDue(1000U + reg::MeasurementMs), "a result should be due on time")
        && expect(sensor.poll(1000U + reg::MeasurementMs), "a poll on time should read")
        && expect(isNear(sensor.readDistance(), 57.0F),
                  "the distance should be what the board reported");

    // Polling again without a new trigger reads nothing more, but keeps the last distance.
    passed = passed
        && expect(!sensor.poll(1000U + reg::MeasurementMs + 1U), "a second poll should do nothing")
        && expect(isNear(sensor.readDistance(), 57.0F),
                  "the last distance should survive a poll with nothing outstanding");

    // Out of range reads 0 on this board, which must not look like an obstacle on the bumper.
    bus.find(reg::FirstAddress)->distanceCm = 0U;
    passed = passed
        && expect(sensor.trigger(2000U), "re-triggering should work")
        && expect(sensor.poll(2000U + reg::MeasurementMs), "the zero reading should be read")
        && expect(std::isnan(sensor.readDistance()), "0 cm should read as no reading, not as 0");

    // Beyond the HC-SR04's own range is no reading either.
    bus.find(reg::FirstAddress)->distanceCm = 500U;
    passed = passed
        && expect(sensor.trigger(3000U), "re-triggering should work")
        && expect(sensor.poll(3000U + reg::MeasurementMs), "the long reading should be read")
        && expect(std::isnan(sensor.readDistance()), "beyond 400 cm should read as no reading");

    // A good reading, then a sensor that goes quiet: the distance must not be held for ever.
    bus.find(reg::FirstAddress)->distanceCm = 120U;
    passed = passed
        && expect(sensor.trigger(4000U), "re-triggering should work")
        && expect(sensor.poll(4000U + reg::MeasurementMs), "the good reading should be read")
        && expect(isNear(sensor.readDistance(), 120.0F), "120 cm should read back");

    const std::uint32_t goneMs{4000U + reg::MeasurementMs};
    bus.find(reg::FirstAddress)->answering = false;
    passed = passed
        && expect(!sensor.poll(goneMs + reg::StaleMs - 1U), "a quiet sensor has nothing to poll")
        && expect(isNear(sensor.readDistance(), 120.0F),
                  "a reading should survive until it is stale")
        && expect(!sensor.poll(goneMs + reg::StaleMs), "a quiet sensor still has nothing to poll")
        && expect(std::isnan(sensor.readDistance()), "a stale reading should be dropped");

    // A failed read must not leave the measurement outstanding for ever: the caller's round
    // robin would stop turning on whichever sensor broke.
    bus.find(reg::FirstAddress)->answering = true;
    passed = passed && expect(sensor.trigger(9000U), "re-triggering should work");
    bus.find(reg::FirstAddress)->answering = false;
    passed = passed
        && expect(!sensor.poll(9000U + reg::MeasurementMs), "a failed read should report failure")
        && expect(!sensor.isResultDue(9000U + reg::MeasurementMs + 1U),
                  "a failed read should clear the outstanding measurement");

    // Three sensors at three addresses are independent, which is what the address pads buy.
    FakeBus three;
    (void)three.init();
    three.add(0x30U, 10U);
    three.add(0x31U, 20U);
    three.add(0x32U, 30U);
    HcSr04Qwiic forward{three, 0x30U};
    HcSr04Qwiic left{three, 0x31U};
    HcSr04Qwiic right{three, 0x32U};
    passed = passed
        && expect(forward.isInitialized() && left.isInitialized() && right.isInitialized(),
                  "all three addresses should come up")
        && expect(forward.trigger(0U) && left.trigger(0U) && right.trigger(0U),
                  "all three should trigger")
        && expect(forward.poll(reg::MeasurementMs) && left.poll(reg::MeasurementMs)
                      && right.poll(reg::MeasurementMs),
                  "all three should read")
        && expect(isNear(forward.readDistance(), 10.0F), "0x30 should read 10 cm")
        && expect(isNear(left.readDistance(), 20.0F), "0x31 should read 20 cm")
        && expect(isNear(right.readDistance(), 30.0F), "0x32 should read 30 cm")
        && expect(three.find(0x30U)->triggers == 1U, "0x30 should have been triggered once")
        && expect(three.find(0x31U)->triggers == 1U, "0x31 should have been triggered once")
        && expect(three.find(0x32U)->triggers == 1U, "0x32 should have been triggered once");

    if (!passed) { return false; }

    std::printf("HC-SR04 Qwiic test succeeded!\n");
    return passed;
}
} // namespace test
