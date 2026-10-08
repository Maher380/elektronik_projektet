/**
 * @file hcsr04_qwiic.cpp
 * @brief HC-SR04 ultrasonic distance sensor on a Qwiic (easyC) I2C adapter.
 */

#include "driver/distance_sensor/hcsr04_qwiic.h"

#include <cmath>
#include <cstdint>

namespace driver::distance_sensor
{

namespace
{

/** Bytes in a distance or duration reading. */
constexpr std::size_t ReadingBytes{2U};

} // namespace

HcSr04Qwiic::HcSr04Qwiic(i2c::Interface& bus, const std::uint8_t address) noexcept
    : myBus{bus}
    , myAddress{address}
    , myInitialized{false}
    , myAwaitingResult{false}
    , myTriggerMs{0U}
    , myResultMs{0U}
    , myHasResult{false}
    , myDistanceCm{0.0F}
{
    if ((address < hcsr04_qwiic::FirstAddress) || (address > hcsr04_qwiic::LastAddress)) { return; }

    myInitialized = myBus.isInitialized() && myBus.probe(address);
}

bool HcSr04Qwiic::trigger(const std::uint32_t nowMs) noexcept
{
    if (!myInitialized) { return false; }

    const std::uint8_t command{hcsr04_qwiic::TriggerRegister};
    if (!myBus.write(myAddress, &command, 1U)) { return false; }

    myTriggerMs      = nowMs;
    myAwaitingResult = true;
    return true;
}

bool HcSr04Qwiic::isResultDue(const std::uint32_t nowMs) const noexcept
{
    if (!myInitialized || !myAwaitingResult) { return false; }

    return (nowMs - myTriggerMs) >= hcsr04_qwiic::MeasurementMs;
}

bool HcSr04Qwiic::poll(const std::uint32_t nowMs) noexcept
{
    // Drop a reading the caller has outlived before anything else, so that a sensor which
    // stops answering goes quiet rather than holding its last distance for ever. This is why
    // the staleness is judged here and not in readDistance(), which is not given the time.
    if (myHasResult && ((nowMs - myResultMs) >= hcsr04_qwiic::StaleMs)) { myHasResult = false; }

    if (!isResultDue(nowMs)) { return false; }

    // The measurement is spoken for either way: a failed read must not be retried for ever
    // against a sensor that has stopped answering, or the caller's round robin stops turning.
    myAwaitingResult = false;

    const std::uint8_t command{hcsr04_qwiic::DistanceRegister};
    std::uint8_t reading[ReadingBytes]{};
    if (!myBus.writeRead(myAddress, &command, 1U, reading, ReadingBytes)) { return false; }

    // Little endian, unlike the A89301's registers on the same bus.
    const std::uint16_t centimetres{
        static_cast<std::uint16_t>(reading[0] | (static_cast<std::uint16_t>(reading[1]) << 8U))};

    myDistanceCm = static_cast<float>(centimetres);
    myResultMs   = nowMs;
    myHasResult  = true;
    return true;
}

float HcSr04Qwiic::readDistance() noexcept
{
    // Stale readings were already dropped by poll(); this call cannot judge age by itself.
    if (!myInitialized || !myHasResult) { return std::nanf(""); }

    // Out of range reads 0 on this board, which would otherwise look like an obstacle touching
    // the bumper. Anything beyond the HC-SR04's own range is no reading either.
    if ((myDistanceCm < hcsr04_qwiic::MinimumCm) || (myDistanceCm > hcsr04_qwiic::MaximumCm))
    {
        return std::nanf("");
    }

    return myDistanceCm;
}

bool HcSr04Qwiic::isInitialized() const noexcept
{
    return myInitialized;
}

std::uint8_t HcSr04Qwiic::address() const noexcept
{
    return myAddress;
}

} // namespace driver::distance_sensor
