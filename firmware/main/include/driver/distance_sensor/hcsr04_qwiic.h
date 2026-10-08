/**
 * @file hcsr04_qwiic.h
 * @brief HC-SR04 ultrasonic distance sensor on a Qwiic (easyC) I2C adapter.
 *
 * The board carries an ATtiny404 that does the trigger pulse and the echo timing itself and
 * reports the result over I2C, so the sensor costs no GPIO pins and no interrupt. Registers
 * and the measurement delay follow the manufacturer's ESP-IDF component,
 * SolderedElectronics/Soldered-Ultrasonic-Qwiic-ESP-IDF-Component.
 *
 * @note The adapter boosts the bus's 3.3 V to the 5 V the HC-SR04 itself needs (TPS613222A),
 *       so SDA and SCL are 3.3 V and need no level shifting. This is the one difference from
 *       driver/distance_sensor/srf05_esp32s3.h, whose echo pin must be divided down.
 */

#pragma once

#include <cstdint>

#include "driver/distance_sensor/interface.h"
#include "driver/i2c/interface.h"

namespace driver::distance_sensor
{

/**
 * @brief Register numbers and limits of the Qwiic adapter.
 */
namespace hcsr04_qwiic
{

/** Write this register number to start a measurement. */
inline constexpr std::uint8_t TriggerRegister{0x00U};

/** Read 2 bytes, little endian, distance in cm. */
inline constexpr std::uint8_t DistanceRegister{0x01U};

/** Read 2 bytes, little endian, echo length in microseconds. */
inline constexpr std::uint8_t DurationRegister{0x02U};

/** Address with no address pad bridged. */
inline constexpr std::uint8_t FirstAddress{0x30U};

/** Address with all three address pads bridged. */
inline constexpr std::uint8_t LastAddress{0x37U};

/**
 * @brief Time from a trigger to a readable result, in milliseconds.
 *
 * The manufacturer's example waits 50 ms. A 400 cm echo takes 23 ms to come back, so this is
 * the full range with room to spare.
 */
inline constexpr std::uint32_t MeasurementMs{50U};

/** Shortest distance the HC-SR04 can measure, in cm. */
inline constexpr float MinimumCm{2.0F};

/** Longest distance the HC-SR04 can measure, in cm. */
inline constexpr float MaximumCm{400.0F};

/** A stored reading older than this is reported as no reading, in milliseconds. */
inline constexpr std::uint32_t StaleMs{500U};

} // namespace hcsr04_qwiic

/**
 * @brief Distance sensor implementation for an HC-SR04 behind a Qwiic I2C adapter.
 *
 * Measuring is split in two so that nothing blocks: trigger() asks the board to ping, and
 * poll() collects the answer once hcsr04_qwiic::MeasurementMs has passed. readDistance()
 * returns whatever poll() stored last and never waits for the bus.
 *
 * @attention Two sensors that ping at the same time hear each other. A caller with several
 *            sensors must trigger them one at a time, which is what isResultDue() is for.
 */
class HcSr04Qwiic final : public Interface
{
public:
    /**
     * @brief Constructor. Probes the sensor on the bus.
     *
     * @param[in] bus Reference to an already initialized I2C bus driver.
     * @param[in] address 7-bit device address, hcsr04_qwiic::FirstAddress to LastAddress.
     */
    HcSr04Qwiic(i2c::Interface& bus, std::uint8_t address) noexcept;

    /**
     * @brief Destructor.
     */
    ~HcSr04Qwiic() noexcept override = default;

    /**
     * @brief Ask the board to start one measurement.
     *
     * @param[in] nowMs Monotonic milliseconds.
     * @return True if the trigger was acknowledged on the bus, false otherwise.
     */
    bool trigger(std::uint32_t nowMs) noexcept;

    /**
     * @brief Check whether a triggered measurement has had time to finish.
     *
     * @param[in] nowMs Monotonic milliseconds.
     * @return True if a measurement is outstanding and its result is now readable.
     */
    bool isResultDue(std::uint32_t nowMs) const noexcept;

    /**
     * @brief Read a finished measurement back and store it.
     *
     * Does nothing unless a measurement is outstanding and isResultDue() holds.
     *
     * @param[in] nowMs Monotonic milliseconds.
     * @return True if a reading was read off the bus, whether or not it was in range.
     */
    bool poll(std::uint32_t nowMs) noexcept;

    /**
     * @brief Read the distance stored by the last poll().
     *
     * @return Distance in cm, or NaN if there is no reading or it was outside the sensor's
     *         range. A reading older than hcsr04_qwiic::StaleMs is dropped by poll(), so this
     *         is only as fresh as the last poll() the caller made.
     */
    float readDistance() noexcept override;

    /**
     * @brief Check if the sensor answered its address when this object was made.
     *
     * @return True if the bus was initialized and the sensor acknowledged.
     */
    bool isInitialized() const noexcept override;

    /**
     * @brief The 7-bit address this sensor answers to.
     *
     * @return The address given to the constructor.
     */
    std::uint8_t address() const noexcept;

    // Delete default constructor, copy/move constructors and assignment operators.
    HcSr04Qwiic()                              = delete;
    HcSr04Qwiic(const HcSr04Qwiic&)            = delete;
    HcSr04Qwiic(HcSr04Qwiic&&)                 = delete;
    HcSr04Qwiic& operator=(const HcSr04Qwiic&) = delete;
    HcSr04Qwiic& operator=(HcSr04Qwiic&&)      = delete;

private:
    /** I2C bus the sensor sits on. */
    i2c::Interface& myBus;

    /** 7-bit device address. */
    const std::uint8_t myAddress;

    /** True if the sensor acknowledged its address when this object was made. */
    bool myInitialized;

    /** True from trigger() until poll() has read the result. */
    bool myAwaitingResult;

    /** Monotonic milliseconds of the last trigger(). */
    std::uint32_t myTriggerMs;

    /** Monotonic milliseconds of the last stored reading. */
    std::uint32_t myResultMs;

    /** True once a reading has been stored. */
    bool myHasResult;

    /** Distance stored by the last poll(), in cm. */
    float myDistanceCm;
};

} // namespace driver::distance_sensor
