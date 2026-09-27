/**
 * @file mpu6050.h
 * @brief IMU driver for the MPU-6050 (3-axis accelerometer + 3-axis gyroscope).
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c/interface.h"
#include "driver/mpu/interface.h"

namespace driver::mpu
{

/**
 * @brief IMU implementation for the MPU-6050 over I2C.
 *
 * The driver uses a shared I2C bus, so it can sit on the same SDA/SCL pins as
 * other I2C devices. The common GY-521 breakout has 4.7 kOhm pull-ups on board.
 *
 * @attention The MPU-6050 has no magnetometer, so yaw from the gyro drifts over time.
 */
class Mpu6050 final : public Interface
{
public:
    /**
     * @brief Constructor.
     *
     * @param[in] i2c I2C bus the MPU-6050 is connected to, must be initialized before init().
     * @param[in] config IMU configuration.
     *
     * The referenced I2C driver must outlive this instance.
     */
    Mpu6050(i2c::Interface& i2c, const Config& config) noexcept;

    /**
     * @brief Destructor.
     */
    ~Mpu6050() noexcept override;

    /**
     * @brief Reset the MPU-6050 and apply the configuration.
     *
     * @return True if the IMU was initialized successfully, false otherwise.
     */
    bool init() noexcept override;

    /**
     * @brief Put the MPU-6050 to sleep. The I2C bus is left untouched.
     *
     * @return True if the IMU was deinitialized successfully, false otherwise.
     */
    bool deinit() noexcept override;

    /**
     * @brief Check if the IMU is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override;

    /**
     * @brief Read acceleration, angular rate and temperature in one burst.
     *
     * @param[out] sample Updated with the new measurement on success, untouched on failure.
     * @return True if a measurement was read, false otherwise.
     */
    bool read(Sample& sample) noexcept override;

    /**
     * @brief Measure the gyro bias by averaging samples. The IMU must be standing still.
     * Blocks for roughly 2 ms per sample.
     *
     * @param[in] sampleCount Number of samples to average.
     * @return True if the bias was measured, false otherwise (previous bias is kept).
     */
    bool calibrateGyro(std::uint16_t sampleCount) noexcept override;

    // Delete default constructor, copy/move constructors and assignment operators.
    Mpu6050()                          = delete;
    Mpu6050(const Mpu6050&)            = delete;
    Mpu6050(Mpu6050&&)                 = delete;
    Mpu6050& operator=(const Mpu6050&) = delete;
    Mpu6050& operator=(Mpu6050&&)      = delete;

private:
    /**
     * @brief Write one byte to a register.
     *
     * @param[in] reg Register address.
     * @param[in] value Value to write.
     * @return True on success, false otherwise.
     */
    bool writeRegister(std::uint8_t reg, std::uint8_t value) noexcept;

    /**
     * @brief Read consecutive registers.
     *
     * @param[in] reg First register address.
     * @param[out] data Buffer for the read bytes.
     * @param[in] length Number of bytes to read.
     * @return True on success, false otherwise.
     */
    bool readRegisters(std::uint8_t reg, std::uint8_t* data, std::size_t length) noexcept;

    /**
     * @brief Read raw gyro values without bias correction.
     *
     * @param[out] rate Angular rate in deg/s.
     * @return True on success, false otherwise.
     */
    bool readRawGyro(Vector3& rate) noexcept;

    /** I2C bus the MPU-6050 is connected to. */
    i2c::Interface& myI2c;

    /** IMU configuration. */
    const Config myConfig;

    /** Accelerometer scale in m/s^2 per LSB. */
    const float myAccelScale;

    /** Gyroscope scale in deg/s per LSB. */
    const float myGyroScale;

    /** Gyro bias in deg/s, subtracted from every reading. */
    Vector3 myGyroBias;

    /** True if init() has succeeded. */
    bool myInitialized;
};

} // namespace driver::mpu
