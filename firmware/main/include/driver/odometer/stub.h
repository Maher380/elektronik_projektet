/**
 * @file stub.h
 * @brief Odometer driver stub for simulation.
 */

#pragma once

#include <cstdint>

#include "driver/odometer/gaps.h"
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
     * @brief Nothing to do: the stub is told its speed rather than timing one.
     */
    void update() noexcept override {}

    /**
     * @brief Accept a gap table and remember it, so a test can read it back.
     *
     * @param[in] table Gap table.
     * @return True if the table was plausible.
     */
    bool setGapTable(const GapTable& table) noexcept override
    {
        if (!isPlausible(table)) { return false; }

        myGapTable = table;
        return true;
    }

    /**
     * @brief Record the simulated direction.
     *
     * @param[in] forward True if the car is being driven forwards.
     */
    void setForward(const bool forward) noexcept override
    {
        myForward = forward;
    }

    /**
     * @brief Report the simulated speed source.
     *
     * A simulated speed is simply given, so it is attributed to the revolution window
     * whenever it is non-zero rather than pretending a phase was recovered.
     *
     * @return The source of the simulated speed.
     */
    SpeedSource speedSource() const noexcept override
    {
        if (!myIsInitialized || (mySpeed == 0.0F)) { return SpeedSource::None; }

        return SpeedSource::Revolution;
    }

    /**
     * @brief A stub never establishes a phase, so it never loses one.
     *
     * @return Always 0.
     */
    std::uint32_t phaseLossCount() const noexcept override
    {
        return 0U;
    }

    /**
     * @brief Report the gap fractions a test has told the stub to observe.
     *
     * Nothing is measured here: a test sets the fractions with simulateObservedGaps() and
     * this hands them back, so a calibration session can be driven through a whole run
     * off-car. Refuses until a test has supplied them, which is how the real driver
     * behaves before a first whole revolution.
     *
     * @param[out] out Buffer for count fractions.
     * @param[in] count Must equal the configured magnet count.
     * @return False if no fractions have been supplied, or on a bad size.
     */
    bool observedGaps(float* out, const std::uint8_t count) const noexcept override
    {
        if ((out == nullptr) || !myHasObservedGaps || (count != myConfig.pulsesPerRevolution))
        {
            return false;
        }

        for (std::uint8_t index{0U}; index < count; ++index)
        {
            out[index] = myObservedGaps[index];
        }
        return true;
    }

    /**
     * @brief Simulate what the next whole revolution measured.
     *
     * @param[in] fractions Gap fractions to report; count must match the configuration.
     * @param[in] count How many fractions.
     * @return False on a bad size, so a test cannot quietly simulate the wrong wheel.
     */
    bool simulateObservedGaps(const float* fractions, const std::uint8_t count) noexcept
    {
        if ((fractions == nullptr) || (count != myConfig.pulsesPerRevolution)
            || (count > MaxPulsesPerRevolution))
        {
            return false;
        }

        for (std::uint8_t index{0U}; index < count; ++index)
        {
            myObservedGaps[index] = fractions[index];
        }
        myHasObservedGaps = true;
        return true;
    }

    /** Forget the simulated gaps, so the stub refuses again as it does before a revolution. */
    void clearObservedGaps() noexcept
    {
        myHasObservedGaps = false;
    }

    /**
     * @brief Read back the gap table the stub was given.
     *
     * @return The stored gap table, empty if none was set.
     */
    const GapTable& gapTable() const noexcept
    {
        return myGapTable;
    }

    /**
     * @brief Read back the simulated direction.
     *
     * @return True if the stub was last told it was going forwards.
     */
    bool isForward() const noexcept
    {
        return myForward;
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

    /** Gap table the stub was last given. */
    GapTable myGapTable{};

    /** Direction the stub was last told. */
    bool myForward{true};

    /** Gap fractions a test has told the stub to observe. */
    float myObservedGaps[MaxPulsesPerRevolution]{};

    /** Whether a test has supplied them; before that the stub refuses, as the driver does. */
    bool myHasObservedGaps{false};
};

} // namespace driver::odometer
