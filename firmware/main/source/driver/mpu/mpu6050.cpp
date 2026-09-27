#include <cstddef>
#include <cstdint>

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/mpu/mpu6050.h"

namespace driver::mpu
{
namespace
{
// MPU-6050 register addresses.
constexpr std::uint8_t RegSampleRateDivider{0x19U};
constexpr std::uint8_t RegConfig{0x1AU};
constexpr std::uint8_t RegGyroConfig{0x1BU};
constexpr std::uint8_t RegAccelConfig{0x1CU};
constexpr std::uint8_t RegAccelXoutH{0x3BU};
constexpr std::uint8_t RegGyroXoutH{0x43U};
constexpr std::uint8_t RegPowerManagement1{0x6BU};
constexpr std::uint8_t RegWhoAmI{0x75U};

// PWR_MGMT_1 values.
constexpr std::uint8_t PowerDeviceReset{0x80U};
constexpr std::uint8_t PowerSleep{0x40U};
constexpr std::uint8_t PowerClockPllGyroX{0x01U};

/** Full-scale range bits sit in bits 3-4 of GYRO_CONFIG and ACCEL_CONFIG. */
constexpr std::uint8_t FullScaleShift{3U};

/**
 * WHO_AM_I values accepted. 0x68 is a genuine MPU-6050, the others are
 * register-compatible clones (MPU-6500 family) often found on cheap GY-521 boards.
 */
constexpr std::uint8_t AcceptedWhoAmI[]{0x68U, 0x70U, 0x72U};

/** Burst of accel XYZ, temperature and gyro XYZ, two bytes each. */
constexpr std::size_t BurstLength{14U};

constexpr std::uint32_t ResetDelayMs{100U};
constexpr std::uint32_t CalibrationSampleDelayUs{2'000U};

// Temperature conversion from the MPU-6050 register map.
constexpr float TemperatureLsbPerDegree{340.0F};
constexpr float TemperatureOffsetDegrees{36.53F};

/** Accelerometer sensitivity for +/- 2 g, halved for each range step. */
constexpr float AccelLsbPerG2{16384.0F};

/** Gyroscope sensitivity per range in LSB per deg/s. */
constexpr float GyroLsbPerDps[]{131.0F, 65.5F, 32.8F, 16.4F};

constexpr float accelScale(const AccelRange range) noexcept
{
    const auto step{static_cast<std::uint8_t>(range)};
    const float lsbPerG{AccelLsbPerG2 / static_cast<float>(1U << step)};
    return StandardGravity / lsbPerG;
}

constexpr float gyroScale(const GyroRange range) noexcept
{
    return 1.0F / GyroLsbPerDps[static_cast<std::uint8_t>(range)];
}

constexpr std::int16_t toInt16(const std::uint8_t high, const std::uint8_t low) noexcept
{
    return static_cast<std::int16_t>((static_cast<std::uint16_t>(high) << 8U) | low);
}

} // namespace

// -----------------------------------------------------------------------------
Mpu6050::Mpu6050(i2c::Interface& i2c, const Config& config) noexcept
    : myI2c{i2c}
    , myConfig{config}
    , myAccelScale{accelScale(config.accelRange)}
    , myGyroScale{gyroScale(config.gyroRange)}
    , myGyroBias{}
    , myInitialized{false}
{}

// -----------------------------------------------------------------------------
Mpu6050::~Mpu6050() noexcept
{
    if (myInitialized)
    {
        deinit();
    }
}

// -----------------------------------------------------------------------------
bool Mpu6050::init() noexcept
{
    // Return false if the IMU is already initialized.
    if (myInitialized) { return false; }

    // Return false if the I2C bus is not ready.
    if (!myI2c.isInitialized()) { return false; }

    // Make sure an MPU-6050 (or compatible) is answering on the bus.
    std::uint8_t whoAmI{0U};
    bool isKnownDevice{false};

    if (readRegisters(RegWhoAmI, &whoAmI, 1U))
    {
        for (const auto accepted : AcceptedWhoAmI)
        {
            if (accepted == whoAmI) { isKnownDevice = true; }
        }
    }

    if (!isKnownDevice) { return false; }

    // Reset all registers, then wake up using the gyro X PLL as a stable clock.
    if (!writeRegister(RegPowerManagement1, PowerDeviceReset)) { return false; }
    vTaskDelay(pdMS_TO_TICKS(ResetDelayMs));

    // The gyro outputs at 8 kHz with the filter off and 1 kHz with it on;
    // divide down to a 1 kHz sample rate in both cases.
    const std::uint8_t sampleRateDivider{
        (myConfig.bandwidth == Bandwidth::Hz260) ? std::uint8_t{7U} : std::uint8_t{0U}};

    const bool configured{
        writeRegister(RegPowerManagement1, PowerClockPllGyroX) &&
        writeRegister(RegSampleRateDivider, sampleRateDivider) &&
        writeRegister(RegConfig, static_cast<std::uint8_t>(myConfig.bandwidth)) &&
        writeRegister(RegGyroConfig,
                      static_cast<std::uint8_t>(static_cast<std::uint8_t>(myConfig.gyroRange) << FullScaleShift)) &&
        writeRegister(RegAccelConfig,
                      static_cast<std::uint8_t>(static_cast<std::uint8_t>(myConfig.accelRange) << FullScaleShift))};

    if (!configured) { return false; }

    myGyroBias    = Vector3{};
    myInitialized = true;
    return true;
}

// -----------------------------------------------------------------------------
bool Mpu6050::deinit() noexcept
{
    // Return false if init() never succeeded.
    if (!myInitialized) { return false; }

    // Put the sensor to sleep to save power.
    const bool asleep{writeRegister(RegPowerManagement1, PowerSleep)};

    myInitialized = false;
    return asleep;
}

// -----------------------------------------------------------------------------
bool Mpu6050::isInitialized() const noexcept
{
    return myInitialized;
}

// -----------------------------------------------------------------------------
bool Mpu6050::read(Sample& sample) noexcept
{
    if (!myInitialized) { return false; }

    // Read everything in one burst so all values come from the same sample.
    std::uint8_t data[BurstLength]{};
    if (!readRegisters(RegAccelXoutH, data, BurstLength)) { return false; }

    sample.acceleration.x = static_cast<float>(toInt16(data[0], data[1])) * myAccelScale;
    sample.acceleration.y = static_cast<float>(toInt16(data[2], data[3])) * myAccelScale;
    sample.acceleration.z = static_cast<float>(toInt16(data[4], data[5])) * myAccelScale;

    sample.temperature = (static_cast<float>(toInt16(data[6], data[7])) / TemperatureLsbPerDegree)
                       + TemperatureOffsetDegrees;

    sample.angularRate.x = (static_cast<float>(toInt16(data[8], data[9])) * myGyroScale) - myGyroBias.x;
    sample.angularRate.y = (static_cast<float>(toInt16(data[10], data[11])) * myGyroScale) - myGyroBias.y;
    sample.angularRate.z = (static_cast<float>(toInt16(data[12], data[13])) * myGyroScale) - myGyroBias.z;

    return true;
}

// -----------------------------------------------------------------------------
bool Mpu6050::calibrateGyro(const std::uint16_t sampleCount) noexcept
{
    if (!myInitialized || (sampleCount == 0U)) { return false; }

    Vector3 sum{};

    for (std::uint16_t i{0U}; i < sampleCount; ++i)
    {
        Vector3 rate{};
        if (!readRawGyro(rate)) { return false; }

        sum.x += rate.x;
        sum.y += rate.y;
        sum.z += rate.z;

        // Wait for a new sample (1 kHz sample rate).
        esp_rom_delay_us(CalibrationSampleDelayUs);
    }

    const float count{static_cast<float>(sampleCount)};
    myGyroBias = Vector3{sum.x / count, sum.y / count, sum.z / count};
    return true;
}

// -----------------------------------------------------------------------------
bool Mpu6050::writeRegister(const std::uint8_t reg, const std::uint8_t value) noexcept
{
    const std::uint8_t data[]{reg, value};
    return myI2c.write(myConfig.address, data, sizeof(data));
}

// -----------------------------------------------------------------------------
bool Mpu6050::readRegisters(const std::uint8_t reg, std::uint8_t* data, const std::size_t length) noexcept
{
    if (!data || (length == 0U)) { return false; }

    // Set the register pointer, then burst-read from it. The MPU-6050 auto-increments
    // the pointer, so consecutive registers come back in one read.
    return myI2c.write(myConfig.address, &reg, 1U) && myI2c.read(myConfig.address, data, length);
}

// -----------------------------------------------------------------------------
bool Mpu6050::readRawGyro(Vector3& rate) noexcept
{
    std::uint8_t data[6U]{};
    if (!readRegisters(RegGyroXoutH, data, sizeof(data))) { return false; }

    rate.x = static_cast<float>(toInt16(data[0], data[1])) * myGyroScale;
    rate.y = static_cast<float>(toInt16(data[2], data[3])) * myGyroScale;
    rate.z = static_cast<float>(toInt16(data[4], data[5])) * myGyroScale;
    return true;
}

} // namespace driver::mpu
