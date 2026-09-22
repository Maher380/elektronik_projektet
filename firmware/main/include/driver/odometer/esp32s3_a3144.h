/**
 * @file esp32s3_a3144.h
 * @brief Odometer driver for ESP32-S3 using an A3144 Hall-effect sensor.
 */

#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"

#include "driver/odometer/interface.h"

namespace driver::odometer
{

/**
 * @brief Odometer implementation using an A3144 Hall-effect sensor.
 *
 * The A3144 has an open-collector, active-low output: it is pulled low while a
 * magnet (south pole) passes the sensor. Pulses are counted by a GPIO interrupt
 * on the falling edge, so no pulses are missed regardless of the main loop rate.
 * The internal pull-up is enabled, so no external pull-up is required.
 *
 * @attention The A3144 needs a 4.5-24 V supply. When supplied with 5 V the output
 *            is still safe for the 3.3 V ESP32-S3 input since it is open-collector
 *            and only pulled up to 3.3 V by the ESP32-S3.
 */
class Esp32s3A3144 final : public Interface
{
public:
    /**
     * @brief Constructor.
     *
     * @param[in] config Odometer configuration.
     */
    explicit Esp32s3A3144(const Config& config) noexcept;

    /**
     * @brief Destructor.
     */
    ~Esp32s3A3144() noexcept override;

    /**
     * @brief Initialize the odometer and start counting pulses.
     *
     * @return True if the odometer was initialized successfully, false otherwise.
     */
    bool init() noexcept override;

    /**
     * @brief Stop counting pulses and release the pin.
     *
     * @return True if the odometer was deinitialized successfully, false otherwise.
     */
    bool deinit() noexcept override;

    /**
     * @brief Check if the odometer is initialized.
     *
     * @return True if initialized and counting, false otherwise.
     */
    bool isInitialized() const noexcept override;

    /**
     * @brief Get the number of pulses counted since init() or the last reset().
     *
     * @return Pulse count, 0 if not initialized.
     */
    std::uint32_t pulseCount() const noexcept override;

    /**
     * @brief Get the distance travelled since init() or the last reset().
     *
     * @return Distance in meters, 0 if not initialized.
     */
    float distance() const noexcept override;

    /**
     * @brief Get the current speed.
     * Calculated from the time between the two latest pulses, which stays accurate
     * at low speeds. Reads as 0 if no pulse has been seen for a while.
     *
     * @return Speed in meters per second, 0 if standing still or not initialized.
     */
    float speed() const noexcept override;

    /**
     * @brief Reset the pulse count (and thereby the distance) to zero.
     */
    void reset() noexcept override;

    // Delete default constructor, copy/move constructors and assignment operators.
    Esp32s3A3144()                               = delete;
    Esp32s3A3144(const Esp32s3A3144&)            = delete;
    Esp32s3A3144(Esp32s3A3144&&)                 = delete;
    Esp32s3A3144& operator=(const Esp32s3A3144&) = delete;
    Esp32s3A3144& operator=(Esp32s3A3144&&)      = delete;

private:
    /**
     * @brief GPIO interrupt handler, called on every falling edge of the sensor output.
     *
     * @param[in] arg Pointer to the Esp32s3A3144 instance.
     */
    static void onPulse(void* arg) noexcept;

    /** Odometer configuration. */
    const Config myConfig;

    /** Distance travelled per pulse in meters. */
    const float myDistancePerPulse;

    /** Guards the fields below, which are shared between the ISR and tasks. */
    mutable portMUX_TYPE myMux = portMUX_INITIALIZER_UNLOCKED;

    /** Number of pulses counted. */
    std::uint32_t myPulseCount;

    /** Timestamp of the latest pulse in microseconds (esp_timer). */
    std::int64_t myLastPulseUs;

    /** Time between the two latest pulses in microseconds, 0 if fewer than two pulses. */
    std::int64_t myPulsePeriodUs;

    /** True if init() has succeeded. */
    bool myInitialized;
};

} // namespace driver::odometer
