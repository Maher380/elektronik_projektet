/**
 * @file stub.h
 * @brief Odometer driver stub for simulation.
 */

#pragma once

#include <cstdint>

#include "driver/odometer/interface.h"

namespace driver::odometer
{

/**
 * @brief Odometer stub implementation.
 * Used to test without hardware.
 */
class Stub final : public Interface
{
public:
    /**
     * @brief Constructor.
     *
     * @param[in] config Odometer configuration, used to convert pulses to distance.
     */
    explicit Stub(const Config& config) noexcept
        : myConfig{config}
        , myPulseCount{0U}
        , mySpeed{0.0F}
        , myIsInitialized{false}
    {}

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Initialize the odometer stub.
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
     * @brief Deinitialize the odometer stub.
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
     * @brief Check if the odometer stub is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override
    {
        return myIsInitialized;
    }

    /**
     * @brief Get the number of simulated pulses.
     *
     * @return Pulse count, 0 if not initialized.
     */
    std::uint32_t pulseCount() const noexcept override
    {
        return myIsInitialized ? myPulseCount : 0U;
    }

    /**
     * @brief Get the simulated distance travelled.
     *
     * @return Distance in meters, 0 if not initialized.
     */
    float distance() const noexcept override
    {
        if (!myIsInitialized) { return 0.0F; }

        return static_cast<float>(myPulseCount) * distancePerPulse(myConfig);
    }

    /**
     * @brief Get the simulated speed.
     *
     * @return Speed in meters per second, 0 if not initialized.
     */
    float speed() const noexcept override
    {
        return myIsInitialized ? mySpeed : 0.0F;
    }

    /**
     * @brief Reset the simulated pulse count to zero.
     */
    void reset() noexcept override
    {
        myPulseCount = 0U;
    }

    /**
     * @brief Simulation of hardware input.
     *
     * @param[in] pulses Number of pulses to add to the pulse count.
     */
    void simulatePulses(const std::uint32_t pulses) noexcept
    {
        myPulseCount += pulses;
    }

    /**
     * @brief Simulate the current speed.
     *
     * @param[in] speed Speed to simulate (in m/s).
     */
    void simulateSpeed(const float speed) noexcept
    {
        mySpeed = speed;
    }

    // Delete copy/move constructors and operators.
    Stub(const Stub&)            = delete;
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(Stub&&)      = delete;

private:
    /** Odometer configuration. */
    const Config myConfig;

    /** Simulated pulse count. */
    std::uint32_t myPulseCount;

    /** Simulated speed in m/s. */
    float mySpeed;

    /** Simulated odometer state. */
    bool myIsInitialized;
};

} // namespace driver::odometer
