/**
 * @file imu_test.cpp
 * @brief MPU-6050 IMU test app, run when IMU_TEST_MODE is defined in main.cpp.
 *
 * Also does a rough IMU-only position estimate for moving the chip by hand, to
 * sanity-check the data. Prototype use only: position comes from integrating
 * acceleration twice, which drifts fast. Velocity is reset to zero whenever the
 * chip is held still, so short "move, stop, move" tests stay within a few cm.
 *
 * @note Remove this file (and the IMU_TEST_MODE define in main.cpp) once no longer needed.
 */

#include "test_app/test_app.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "driver/i2c/esp32s3.h"
#include "driver/mpu/mpu6050.h"
#include "driver/serial/esp32s3.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app::test_app
{
namespace
{
using driver::mpu::Vector3;

constexpr float RadToDeg{57.2957795F};
constexpr float DegToRad{1.0F / RadToDeg};

// Stillness detection: all of these must hold for StillTimeUs to count as standing still.
constexpr float StillAccelToleranceMs2{0.3F};   // | |a| - g | below this.
constexpr float StillGyroToleranceDps{3.0F};    // |gyro| below this.
constexpr std::int64_t StillTimeUs{100'000};

/** Rotation matrix, row-major. Maps sensor frame to the start frame. */
struct Matrix3
{
    float m[3][3]{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
};

float length(const Vector3& v) noexcept
{
    return std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
}

Vector3 rotate(const Matrix3& r, const Vector3& v) noexcept
{
    return Vector3{(r.m[0][0] * v.x) + (r.m[0][1] * v.y) + (r.m[0][2] * v.z),
                   (r.m[1][0] * v.x) + (r.m[1][1] * v.y) + (r.m[1][2] * v.z),
                   (r.m[2][0] * v.x) + (r.m[2][1] * v.y) + (r.m[2][2] * v.z)};
}

/**
 * @brief Apply a small rotation (gyro rate in deg/s over dt) to the orientation.
 * Uses R = R * (I + [w]x * dt), then re-orthonormalizes to stop numerical creep.
 */
void integrateGyro(Matrix3& r, const Vector3& rateDps, const float dtS) noexcept
{
    const float wx{rateDps.x * DegToRad * dtS};
    const float wy{rateDps.y * DegToRad * dtS};
    const float wz{rateDps.z * DegToRad * dtS};

    Matrix3 next{};
    for (int i{0}; i < 3; ++i)
    {
        const float a{r.m[i][0]};
        const float b{r.m[i][1]};
        const float c{r.m[i][2]};
        next.m[i][0] = a + (b * wz) - (c * wy);
        next.m[i][1] = b - (a * wz) + (c * wx);
        next.m[i][2] = c + (a * wy) - (b * wx);
    }

    // Gram-Schmidt on the rows.
    for (int i{0}; i < 3; ++i)
    {
        for (int j{0}; j < i; ++j)
        {
            const float dot{(next.m[i][0] * next.m[j][0]) + (next.m[i][1] * next.m[j][1]) +
                            (next.m[i][2] * next.m[j][2])};
            for (int k{0}; k < 3; ++k) { next.m[i][k] -= dot * next.m[j][k]; }
        }
        const float norm{std::sqrt((next.m[i][0] * next.m[i][0]) + (next.m[i][1] * next.m[i][1]) +
                                   (next.m[i][2] * next.m[i][2]))};
        if (norm > 0.0F)
        {
            for (int k{0}; k < 3; ++k) { next.m[i][k] /= norm; }
        }
    }
    r = next;
}

/** Tilt around X from gravity, in degrees. */
float rollDeg(const Vector3& a) noexcept
{
    return std::atan2(a.y, a.z) * RadToDeg;
}

/** Tilt around Y from gravity, in degrees. */
float pitchDeg(const Vector3& a) noexcept
{
    return std::atan2(-a.x, std::sqrt((a.y * a.y) + (a.z * a.z))) * RadToDeg;
}

/** Heading around the start frame's Z axis, in degrees. */
float yawDeg(const Matrix3& r) noexcept
{
    return std::atan2(r.m[1][0], r.m[0][0]) * RadToDeg;
}

} // namespace

void runImuTest() noexcept
{
    constexpr std::uint8_t sdaPin{11U};                 // A4 / GPIO11
    constexpr std::uint8_t sclPin{12U};                 // A5 / GPIO12
    constexpr std::uint32_t pollPeriodMs{10U};          // How often the IMU is read (integration rate).
    constexpr std::int64_t reportPeriodUs{200'000};     // How often the status line is printed.
    constexpr std::uint16_t calibrationSamples{500U};   // ~1 s, the IMU must stand still.
    constexpr std::uint16_t gravitySamples{50U};        // Averaged to measure gravity at the start.
    constexpr const char* helpText{
        "Commands: c = calibrate gyro (keep still), z = zero position/heading here (keep still),\n"
        "          p = pause/resume printing, i = init, d = deinit, h = help\n"
        "Position is in the frame the chip had at the last z. Velocity resets while held still,\n"
        "so move it, stop for a moment, move again. Expect cm-level errors, growing while moving.\n"};

    driver::serial::Esp32s3 serial(driver::serial::Config{
        .port = UART_NUM_0,
        .txPin = UART_PIN_NO_CHANGE,
        .rxPin = UART_PIN_NO_CHANGE,
        .baudRate = 115200,
        .rxBufSize = 256U,
        .useUsbJtag = true,
    });
    serial.connect();

    char buf[320]{'\0'};

    driver::i2c::Esp32s3 i2c(driver::i2c::Config{.sdaPin = sdaPin, .sclPin = sclPin});
    driver::mpu::Mpu6050 imu(i2c, driver::mpu::Config{});

    // Pose state, in the start frame.
    Matrix3 orientation{};
    Vector3 gravity{0.0F, 0.0F, driver::mpu::StandardGravity};
    Vector3 velocity{};
    Vector3 position{};
    float gravityLength{driver::mpu::StandardGravity};
    std::int64_t stillSinceUs{0};
    bool isStill{false};

    // Make the current pose the new start: measure gravity, clear orientation, velocity and position.
    auto zeroPose = [&]() -> bool {
        Vector3 sum{};
        std::uint16_t count{0U};
        driver::mpu::Sample s{};
        for (std::uint16_t i{0U}; i < gravitySamples; ++i)
        {
            if (imu.read(s))
            {
                sum.x += s.acceleration.x;
                sum.y += s.acceleration.y;
                sum.z += s.acceleration.z;
                ++count;
            }
            vTaskDelay(pdMS_TO_TICKS(pollPeriodMs));
        }
        if (count == 0U) { return false; }

        const float n{static_cast<float>(count)};
        gravity       = Vector3{sum.x / n, sum.y / n, sum.z / n};
        gravityLength = length(gravity);
        orientation   = Matrix3{};
        velocity      = Vector3{};
        position      = Vector3{};
        return true;
    };

    std::snprintf(buf, sizeof(buf), "\nIMU test: MPU-6050 on SDA GPIO%u, SCL GPIO%u, address 0x%02X\n",
                  sdaPin, sclPin, driver::mpu::Config{}.address);
    serial.write(buf);
    serial.write(i2c.init() ? "I2C init OK\n" : "I2C init FAILED (pins invalid or taken)\n");
    serial.write(i2c.probe(driver::mpu::Config{}.address) ? "IMU answers on the bus\n"
                                                          : "IMU does NOT answer (check wiring, AD0, pull-ups)\n");
    serial.write(imu.init() ? "IMU init OK\n" : "IMU init FAILED (wrong WHO_AM_I or bus error)\n");

    if (imu.isInitialized())
    {
        serial.write("Calibrating gyro and measuring gravity, keep still...\n");
        serial.write((imu.calibrateGyro(calibrationSamples) && zeroPose()) ? "Calibration OK, position zeroed\n"
                                                                         : "Calibration FAILED\n");
    }
    serial.write(helpText);

    bool printing{true};
    std::uint32_t readErrors{0U};
    std::int64_t lastReadUs{esp_timer_get_time()};
    std::int64_t lastReportUs{lastReadUs};
    driver::mpu::Sample sample{};

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(pollPeriodMs));
        const std::int64_t nowUs{esp_timer_get_time()};

        // Handle commands.
        if (serial.isDataAvailable())
        {
            char line[16]{'\0'};
            serial.read(line, sizeof(line));

            if (std::strcmp(line, "c") == 0)
            {
                serial.write("Calibrating gyro and measuring gravity, keep still...\n");
                serial.write((imu.calibrateGyro(calibrationSamples) && zeroPose())
                                 ? "Calibration OK, position zeroed\n"
                                 : "Calibration FAILED\n");
            }
            else if (std::strcmp(line, "z") == 0)
            {
                serial.write(zeroPose() ? "Position and heading zeroed, this is the new start\n"
                                        : "Zero FAILED (IMU not initialized?)\n");
            }
            else if (std::strcmp(line, "p") == 0)
            {
                printing = !printing;
                serial.write(printing ? "Printing resumed\n" : "Printing paused\n");
            }
            else if (std::strcmp(line, "i") == 0)
            {
                serial.write(imu.init() ? "Init OK (run c to calibrate)\n" : "Init failed (already initialized?)\n");
            }
            else if (std::strcmp(line, "d") == 0)
            {
                serial.write(imu.deinit() ? "Deinit OK, IMU is asleep\n" : "Deinit failed (not initialized?)\n");
            }
            else { serial.write(helpText); }

            // Commands can block (calibration), don't integrate over that gap.
            lastReadUs = esp_timer_get_time();
            continue;
        }

        if (imu.isInitialized())
        {
            if (imu.read(sample))
            {
                const float dtS{static_cast<float>(nowUs - lastReadUs) * 1.0e-6F};

                // Track orientation, then express acceleration in the start frame and remove gravity.
                integrateGyro(orientation, sample.angularRate, dtS);
                const Vector3 accelStart{rotate(orientation, sample.acceleration)};
                const Vector3 motion{accelStart.x - gravity.x, accelStart.y - gravity.y, accelStart.z - gravity.z};

                // Standing still: acceleration is just gravity and nothing rotates.
                const bool stillNow{(std::fabs(length(sample.acceleration) - gravityLength) < StillAccelToleranceMs2) &&
                                    (length(sample.angularRate) < StillGyroToleranceDps)};
                if (!stillNow) { stillSinceUs = nowUs; }
                isStill = (nowUs - stillSinceUs) >= StillTimeUs;

                if (isStill)
                {
                    velocity = Vector3{}; // Zero-velocity update, stops drift from piling up.
                }
                else
                {
                    velocity.x += motion.x * dtS;
                    velocity.y += motion.y * dtS;
                    velocity.z += motion.z * dtS;
                    position.x += velocity.x * dtS;
                    position.y += velocity.y * dtS;
                    position.z += velocity.z * dtS;
                }
            }
            else { ++readErrors; }
        }
        lastReadUs = nowUs;

        // Print a status line every report period.
        if (printing && ((nowUs - lastReportUs) >= reportPeriodUs))
        {
            std::snprintf(buf, sizeof(buf),
                          "[%s] %s | pos x %6.3f y %6.3f z %6.3f m (%.3f m from start) | speed %5.2f m/s | "
                          "roll %6.1f pitch %6.1f yaw %7.1f deg | gyro %6.1f %6.1f %6.1f dps | err %lu\n",
                          imu.isInitialized() ? "ON " : "OFF",
                          isStill ? "still " : "MOVING",
                          static_cast<double>(position.x),
                          static_cast<double>(position.y),
                          static_cast<double>(position.z),
                          static_cast<double>(length(position)),
                          static_cast<double>(length(velocity)),
                          static_cast<double>(rollDeg(sample.acceleration)),
                          static_cast<double>(pitchDeg(sample.acceleration)),
                          static_cast<double>(yawDeg(orientation)),
                          static_cast<double>(sample.angularRate.x),
                          static_cast<double>(sample.angularRate.y),
                          static_cast<double>(sample.angularRate.z),
                          static_cast<unsigned long>(readErrors));
            serial.write(buf);
            lastReportUs = nowUs;
        }
    }
}

} // namespace app::test_app
