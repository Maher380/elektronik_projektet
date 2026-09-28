/**
 * @file ford.h
 * @brief Ford: pulse-width steering servo. No motor yet, so it cannot drive.
 */

#pragma once

#include "system/car/interface.h"

#include <cstdint>
#include <memory>

namespace driver::factory { class Interface; }
namespace driver::pwm { class Interface; }

namespace app::car
{

/**
 * @brief The Ford car.
 *
 * Parts are added as Ford's hardware is decided. If the car ID pin says the firmware
 * is not running on Ford, no drivers are created and the car has no parts.
 */
class Ford final : public Interface
{
public:
    explicit Ford(driver::factory::Interface& factory) noexcept;
    ~Ford() noexcept override;

    bool init() noexcept override;
    void deinit() noexcept override;
    driver::motor::Interface* motor() noexcept override;
    driver::servo::Interface* steering() noexcept override;
    driver::odometer::Interface* odometer() noexcept override;
    bool readObstacleDistances(navigation::Distances& distances) noexcept override;
    const char* problem() const noexcept override;

private:
    // Steering servo
    static constexpr std::uint8_t steeringServoPwmPin{9U};        // D6 / GPIO9
    static constexpr std::uint32_t steeringServoPwmFrequencyHz{50U};

    const bool myIsOnCar;
    std::unique_ptr<driver::pwm::Interface> mySteeringServoPwm;
    std::unique_ptr<driver::servo::Interface> mySteeringServo;
};

} // namespace app::car
