/**
 * @file id.h
 * @brief Car ID pin, used to check that a build runs on its target car.
 */

#pragma once

#include <cstdint>

namespace driver::factory { class Interface; }

namespace app::car
{

/** The cars the firmware can be built for. */
enum class Id : std::uint8_t
{
    Vagrant,
    Ford,
};

/** Car ID pin, reserved on every car: D10 / GPIO21. */
inline constexpr std::uint8_t IdPin{21U};

/**
 * @brief Check whether a car ID pin level belongs to a car.
 *
 * The pin is read with the internal pull-up enabled. Vagrant leaves it open (high),
 * Ford ties it to GND (low).
 *
 * @param[in] car The car to check for.
 * @param[in] pinLevel The level read on the car ID pin.
 * @return True if the level belongs to the car.
 */
constexpr bool isIdPinLevelOf(const Id car, const bool pinLevel) noexcept
{
    return pinLevel == (car == Id::Vagrant);
}

/**
 * @brief Get the lower-case name of a car, as used in the car name.
 *
 * @param[in] car The car.
 * @return The car's name.
 */
constexpr const char* toString(const Id car) noexcept
{
    return car == Id::Vagrant ? "vagrant" : "ford";
}

/**
 * @brief Read the car ID pin to find out which car the firmware runs on.
 *
 * The pin is released again after reading.
 *
 * @param[in] factory Factory used to create the GPIO input.
 * @param[out] car The car the pin belongs to.
 * @return True if the pin was read, false if it could not be read.
 */
bool readHardwareCar(driver::factory::Interface& factory, Id& car) noexcept;

/**
 * @brief Read the car ID pin and check whether the firmware runs on the given car.
 *
 * The pin is released again after reading.
 *
 * @param[in] factory Factory used to create the GPIO input.
 * @param[in] car The car to check for.
 * @return True if the car ID pin belongs to the car, false otherwise or if it cannot be read.
 */
bool isRunningOn(driver::factory::Interface& factory, Id car) noexcept;

} // namespace app::car
