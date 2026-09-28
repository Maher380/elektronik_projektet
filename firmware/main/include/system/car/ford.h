/**
 * @file ford.h
 * @brief Ford: no parts yet, so it cannot drive.
 */

#pragma once

#include "system/car/interface.h"

namespace driver::factory { class Interface; }

namespace app::car
{

/**
 * @brief The Ford car.
 *
 * Parts are added as Ford's hardware is decided.
 */
class Ford final : public Interface
{
public:
    explicit Ford(driver::factory::Interface& factory) noexcept;
    ~Ford() noexcept override = default;

    bool init() noexcept override;
    void deinit() noexcept override;
    driver::motor::Interface* motor() noexcept override;
    driver::servo::Interface* steering() noexcept override;
    driver::odometer::Interface* odometer() noexcept override;
    bool readObstacleDistances(navigation::Distances& distances) noexcept override;
};

} // namespace app::car
