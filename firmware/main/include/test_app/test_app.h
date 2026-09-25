/**
 * @file test_app.h
 * @brief Minimal test apps, selected by the test mode defines in main.cpp.
 */

#pragma once

namespace app::test_app
{

/**
 * @brief Run the driver test app (timer, ADC and GPIO).
 *
 * @note Never returns.
 */
[[noreturn]] void runDriverTest() noexcept;

/**
 * @brief Run the odometer-only test app.
 *
 * Prints a line for every counted pulse and a status line every second.
 * Commands (type + Enter): r = reset, i = init, d = deinit, h = help.
 *
 * @note Never returns.
 */
[[noreturn]] void runOdometerTest() noexcept;

/**
 * @brief Run the A89301 motor test app.
 *
 * Drives the A89301 with PWM on SPD. Commands (type + Enter): 0.0 - 1.0 = speed,
 * f = forward, b = backward, s = stop (coast), x = stop (brake), h = help.
 *
 * @note Never returns.
 */
[[noreturn]] void runMotorTest() noexcept;

} // namespace app::test_app
