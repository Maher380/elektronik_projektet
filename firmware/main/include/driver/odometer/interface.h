/**
 * @file interface.h
 * @brief Abstract odometer driver interface.
 */

#pragma once

#include <cstdint>

namespace driver::odometer
{

/**
 * @brief Configuration for an odometer.
 */
struct Config
{
    /** GPIO pin connected to the sensor output. */
    std::uint8_t pin{0U};

    /** Number of pulses the sensor produces per wheel revolution (i.e. number of magnets). */
    std::uint8_t pulsesPerRevolution{1U};

    /** Wheel diameter in meters. */
    float wheelDiameterM{0.0F};
};

/**
 * @brief Ratio of a circle's circumference to its diameter.
 */
inline constexpr float Pi{3.14159265358979323846F};

/**
 * @brief Compute the distance travelled per sensor pulse from a Config.
 *
 * @param[in] config Odometer configuration.
 * @return Distance in meters per pulse, 0 if pulsesPerRevolution is 0.
 */
inline constexpr float distancePerPulse(const Config& config) noexcept
{
    if (config.pulsesPerRevolution == 0U) { return 0.0F; }

    const float wheelCircumferenceM{Pi * config.wheelDiameterM};
    return wheelCircumferenceM / static_cast<float>(config.pulsesPerRevolution);
}

/**
 * @brief Abstract interface for odometer drivers.
 */
class Interface
{

public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Initialize the odometer and start counting pulses.
     *
     * @return True if the odometer was initialized successfully, false otherwise.
     */
    virtual bool init() noexcept = 0;

    /**
     * @brief Stop counting pulses and release the hardware.
     *
     * @return True if the odometer was deinitialized successfully, false otherwise.
     */
    virtual bool deinit() noexcept = 0;

    /**
     * @brief Check if the odometer is initialized.
     *
     * @return True if initialized and counting, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;

    /**
     * @brief Get the number of pulses counted since init() or the last reset().
     *
     * @return Pulse count, 0 if not initialized.
     */
    virtual std::uint32_t pulseCount() const noexcept = 0;

    /**
     * @brief Get the distance travelled since init() or the last reset().
     *
     * @return Distance in meters, 0 if not initialized.
     */
    virtual float distance() const noexcept = 0;

    /**
     * @brief Get the current speed.
     *
     * @return Speed in meters per second, 0 if standing still or not initialized.
     */
    virtual float speed() const noexcept = 0;

    /**
     * @brief Reset the pulse count (and thereby the distance) to zero.
     */
    virtual void reset() noexcept = 0;
};

} // namespace driver::odometer
