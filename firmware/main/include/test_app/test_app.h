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

} // namespace app::test_app
