/**
 * @file stub.h
 * @brief IMU driver stub for simulation.
 */

#pragma once

#include <cstdint>

#include "driver/mpu/interface.h"

namespace driver::mpu
{

/**
 * @brief IMU stub implementation.
 * Used to test without hardware. Reports a level, stationary IMU by default.
 */
class Stub final : public Interface
{
public:
    /**
     * @brief Constructor.
     */
    Stub() noexcept
        : mySample{}
        , myIsInitialized{false}
    {
        mySample.acceleration.z = StandardGravity;
        mySample.temperature    = 25.0F;
    }

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Initialize the IMU stub.
     *
     * @return True if initialized successfully, false if already initialized.
     */
    bool init() noexcept override
    {
        if (myIsInitialized) { return false; }
        myIsInitialized = true;
        return true;
    }

    /**
     * @brief Deinitialize the IMU stub.
     *
     * @return True if deinitialized successfully, false if not initialized.
     */
    bool deinit() noexcept override
    {
        if (!myIsInitialized) { return false; }
        myIsInitialized = false;
        return true;
    }

    /**
     * @brief Check if the IMU stub is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override
    {
        return myIsInitialized;
    }

    /**
     * @brief Read the simulated measurement.
     *
     * @param[out] sample Updated with the simulated measurement if initialized.
     * @return True if initialized, false otherwise.
     */
    bool read(Sample& sample) noexcept override
    {
        if (!myIsInitialized) { return false; }
        sample = mySample;
        return true;
    }

    /**
     * @brief Simulated gyro calibration, does nothing.
     *
     * @param[in] sampleCount Number of samples to average (must be non-zero).
     * @return True if initialized and sampleCount is non-zero, false otherwise.
     */
    bool calibrateGyro(const std::uint16_t sampleCount) noexcept override
    {
        return myIsInitialized && (sampleCount != 0U);
    }

    /**
     * @brief Simulate the measurement returned by read().
     *
     * @param[in] sample Measurement to simulate.
     */
    void simulateSample(const Sample& sample) noexcept
    {
        mySample = sample;
    }

    // Delete copy/move constructors and operators.
    Stub(const Stub&)            = delete;
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(Stub&&)      = delete;

private:
    /** Simulated measurement. */
    Sample mySample;

    /** Simulated IMU state. */
    bool myIsInitialized;
};

} // namespace driver::mpu
