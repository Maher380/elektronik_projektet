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
 * @brief Run the SRF05-only test app.
 *
 * Reads the sensor every poll period and prints the distance and the number of valid
 * readings every report period, plus the readDistance() execution time every 10 s.
 *
 * @note Never returns.
 */
[[noreturn]] void runSrf05Test() noexcept;
/**
 * @brief Run the A89301 motor test app.
 *
 * Drives the A89301 with PWM on SPD. Commands (type + Enter): 0.0 - 1.0 = speed,
 * f = forward, b = backward, s = stop (coast), x = stop (brake), h = help.
 *
 * @note Never returns.
 */
[[noreturn]] void runMotorTest() noexcept;

/**
 * @brief Run the start module test app.
 *
 * Polls the start module on GPIO8 (~D5) every second and prints the pin level and
 * the start module state (waiting, started or stopped).
 *
 * @note Never returns.
 */
[[noreturn]] void runStartModuleTest() noexcept;

/**
 * @brief Run the A89301 configuration app.
 *
 * Reads, changes and saves the A89301 settings over I2C, with a live monitor, PID sweeps,
 * speed profiles and motor temperature and stall protection. Type h for the commands.
 *
 * @note Never returns.
 */
[[noreturn]] void runA89301ConfigTest() noexcept;

} // namespace app::test_app
