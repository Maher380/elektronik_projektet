/**
 * @file srf05.h
 * @brief SRF05 ultrasonic distance sensor driver.
 */

#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"

#include "driver/distance_sensor/interface.h"
#include "driver/gpio/interface.h"

namespace driver::distance_sensor
{

/**
 * @brief Distance sensor implementation using an SRF05 ultrasonic sensor in 2-pin mode.
 *
 * A measurement is started by a >= 10 us high pulse on the trigger pin. The sensor then
 * holds the echo pin high for as long as the sound takes to reach the object and return,
 * roughly 58 us per cm of distance. Both echo edges are timestamped by a GPIO interrupt,
 * so readDistance() never waits for the echo: it returns the latest completed measurement
 * and starts a new one when the sensor is ready for it.
 *
 * @attention The SRF05 needs a 5 V supply and drives the echo pin at 5 V. The echo pin
 *            must be connected via a voltage divider (or level shifter), since the
 *            ESP32-S3 is not 5 V tolerant. The 3.3 V trigger output needs no shifting.
 */
class SRF05 final : public Interface
{
public:
    /**
     * @brief Constructor. Enables the echo interrupt.
     *
     * @param[in] trigger Reference to the GPIO output connected to the sensor trigger pin.
     * @param[in] echo Reference to the GPIO input connected to the sensor echo pin.
     */
    SRF05(gpio::Interface& trigger, gpio::Interface& echo) noexcept;

    /**
     * @brief Destructor. Disables the echo interrupt.
     */
    ~SRF05() noexcept override;

    /**
     * @brief Read the latest measured distance and start a new measurement when due.
     *
     * The returned value is from the previous completed measurement, i.e. it is at most
     * one measurement period old when this function is called frequently.
     *
     * @return Distance in cm, or NaN if no valid reading is available (no echo, out of
     *         range, stale or not initialized).
     */
    float readDistance() noexcept override;

    /**
     * @brief Check if the sensor is initialized.
     *
     * @return True if both GPIOs are initialized and the echo interrupt is enabled.
     */
    bool isInitialized() const noexcept override;

    // Delete default constructor, copy/move constructors and assignment operators.
    SRF05()                        = delete;
    SRF05(const SRF05&)            = delete;
    SRF05(SRF05&&)                 = delete;
    SRF05& operator=(const SRF05&) = delete;
    SRF05& operator=(SRF05&&)      = delete;

private:
    /**
     * @brief GPIO interrupt handler, called on every edge of the echo pin.
     *
     * @param[in] arg Pointer to the SRF05 instance.
     */
    static void onEcho(void* arg) noexcept;

    /**
     * @brief Send a trigger pulse to start a new measurement.
     *
     * @param[in] nowUs Current time in microseconds (esp_timer).
     */
    void trigger(std::int64_t nowUs) noexcept;

    /** GPIO connected to the sensor trigger pin. */
    gpio::Interface& myTrigger;

    /** GPIO connected to the sensor echo pin. */
    gpio::Interface& myEcho;

    /** Guards the fields below, which are shared between the ISR and tasks. */
    mutable portMUX_TYPE myMux = portMUX_INITIALIZER_UNLOCKED;

    /** Timestamp of the latest trigger pulse in microseconds (esp_timer). */
    std::int64_t myTriggerUs;

    /** Timestamp of the echo rising edge in microseconds (esp_timer). */
    std::int64_t myEchoStartUs;

    /** Timestamp of the latest completed measurement in microseconds, 0 if none. */
    std::int64_t myResultUs;

    /** Echo pulse width of the latest completed measurement in microseconds, 0 if no echo. */
    std::int64_t myEchoWidthUs;

    /** True from a trigger pulse until the measurement is completed or timed out. */
    bool myAwaitingEcho;

    /** True while the echo pin is high during a measurement. */
    bool myEchoHigh;

    /** True if the echo interrupt was enabled successfully. */
    bool myInitialized;
};

} // namespace driver::distance_sensor
