/**
 * @file logic.h
 * @brief Declaration of the main application logic.
 */

#pragma once

#include "driver/factory/interface.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace app::logic {

/**
 * @brief Main system logic for the autonomous car starter application.
 *
 * The logic layer owns driver interfaces and stays independent from ESP-IDF
 * implementation details.
 */
class Logic final {
public:
    explicit Logic(driver::factory::Interface& factory) noexcept;
    ~Logic() noexcept;

    void run(const std::atomic<bool>& stop) noexcept;

    Logic(const Logic&) = delete;
    Logic& operator=(const Logic&) = delete;
    Logic(Logic&&) = delete;
    Logic& operator=(Logic&&) = delete;

private:
    void setStartState() noexcept;
    void initializeDrivers() noexcept;
    void processWifi() noexcept;
    void processTimer() noexcept;

    std::unique_ptr<driver::serial::Interface> mySerial;
    std::unique_ptr<driver::gpio::Interface> myLed;
    std::unique_ptr<driver::timer::Interface> myTimer;
    std::unique_ptr<driver::wifi::Interface> myWifi;

    bool myBlinkEnabled{false};
    std::uint32_t myPeriodMs{500U};
};

} // namespace app::logic
