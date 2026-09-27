/**
 * @file interface.h
 * @brief Abstract inertial measurement unit (IMU) driver interface.
 */

#pragma once

#include <cstdint>

namespace driver::mpu
{

/**
 * @brief Accelerometer full-scale range.
 */
enum class AccelRange : std::uint8_t
{
    G2,  ///< +/- 2 g.
    G4,  ///< +/- 4 g.
    G8,  ///< +/- 8 g.
    G16, ///< +/- 16 g.
};

/**
 * @brief Gyroscope full-scale range.
 */
enum class GyroRange : std::uint8_t
{
    Dps250,  ///< +/- 250 deg/s.
    Dps500,  ///< +/- 500 deg/s.
    Dps1000, ///< +/- 1000 deg/s.
    Dps2000, ///< +/- 2000 deg/s.
};

/**
 * @brief Digital low-pass filter bandwidth (applies to both accelerometer and gyroscope).
 */
enum class Bandwidth : std::uint8_t
{
    Hz260, ///< ~260 Hz, filter effectively off.
    Hz184, ///< ~184 Hz.
    Hz94,  ///< ~94 Hz.
    Hz44,  ///< ~44 Hz.
    Hz21,  ///< ~21 Hz.
    Hz10,  ///< ~10 Hz.
    Hz5,   ///< ~5 Hz.
};

/**
 * @brief Configuration for an IMU.
 */
struct Config
{
    /** 7-bit I2C address (0x68 with AD0 low, 0x69 with AD0 high). */
    std::uint8_t address{0x68U};

    /** Accelerometer full-scale range. */
    AccelRange accelRange{AccelRange::G4};

    /** Gyroscope full-scale range. */
    GyroRange gyroRange{GyroRange::Dps500};

    /** Digital low-pass filter bandwidth. */
    Bandwidth bandwidth{Bandwidth::Hz44};
};

/**
 * @brief Three-axis vector.
 */
struct Vector3
{
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

/**
 * @brief One IMU measurement.
 */
struct Sample
{
    /** Acceleration in m/s^2 (includes gravity). */
    Vector3 acceleration{};

    /** Angular rate in deg/s, with the gyro bias from calibrateGyro() removed. */
    Vector3 angularRate{};

    /** Die temperature in degrees Celsius. */
    float temperature{0.0F};
};

/**
 * @brief Standard gravity in m/s^2.
 */
inline constexpr float StandardGravity{9.80665F};

/**
 * @brief Abstract interface for IMU drivers.
 */
class Interface
{

public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Initialize the IMU and apply the configuration.
     *
     * @return True if the IMU was initialized successfully, false otherwise.
     */
    virtual bool init() noexcept = 0;

    /**
     * @brief Put the IMU to sleep and release the hardware.
     *
     * @return True if the IMU was deinitialized successfully, false otherwise.
     */
    virtual bool deinit() noexcept = 0;

    /**
     * @brief Check if the IMU is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;

    /**
     * @brief Read acceleration, angular rate and temperature.
     *
     * @param[out] sample Updated with the new measurement on success, untouched on failure.
     * @return True if a measurement was read, false otherwise.
     */
    virtual bool read(Sample& sample) noexcept = 0;

    /**
     * @brief Measure the gyro bias by averaging samples. The IMU must be standing still.
     * The bias is subtracted from every following read().
     *
     * @param[in] sampleCount Number of samples to average.
     * @return True if the bias was measured, false otherwise (previous bias is kept).
     */
    virtual bool calibrateGyro(std::uint16_t sampleCount) noexcept = 0;
};

} // namespace driver::mpu
