/**
 * @file gapCalibration.h
 * @brief The magnet gap calibration, as a state machine driven one tick at a time.
 *
 * This file deliberately knows nothing about ESP-IDF, FreeRTOS or MQTT: it is handed the
 * time, whether the car is armed, a motor temperature and an Odometer, and it answers with
 * the duty to hold. That is what lets a host test drive a whole calibration through the
 * odometer stub, which no logic in this firmware could be before. See ADR 0009.
 *
 * It is a state machine rather than a routine because the shared loop must keep servicing
 * the operator's heartbeat. The measurement takes about a minute and the heartbeat times
 * out after three seconds, so a blocking version would disarm the car and brake the wheel
 * in the middle of the run it was measuring.
 */

#pragma once

#include <cstdint>

#include "driver/odometer/gaps.h"
#include "system/ford.h"

namespace driver::odometer { class Interface; }

namespace app::logic
{

/**
 * @brief Measures one wheel's magnet gaps by driving it at several steady duties.
 *
 * The recipe lives in system/ford.h and is shared with the A89301 configuration app, so
 * both measurements are comparable. Why it needs several speeds rather than more
 * revolutions at one is the whole argument of ADR 0008: Ford turns two motor commutations
 * per magnet gap, so torque ripple biases the same gaps the same way on every revolution
 * and averaging converges on the wrong answer. Only speed separates geometry from ripple.
 */
class GapCalibration final
{
public:
    /** Where a run has got to. */
    enum class Phase : std::uint8_t
    {
        /** Nothing has been measured and nothing is running. */
        Idle,
        /** Holding a duty, waiting for the wheel speed to steady before measuring. */
        Settling,
        /** Averaging whole revolutions at the current duty. */
        Sampling,
        /** Every duty measured and they agree: a table is waiting to be confirmed. */
        Measured,
        /** The run ended without a table. See failure(). */
        Failed,
    };

    /** Why a run produced no table. */
    enum class Failure : std::uint8_t
    {
        None,
        /** No Odometer, so there is nothing to measure with. */
        NoOdometer,
        /** The wheel stopped turning. Is the battery on and the wheel free? */
        Stalled,
        /** The motor can went above its limit. */
        TooHot,
        /** The speeds disagree: that is the motor's ripple, not the wheel's shape. */
        Disagreed,
        /** The averaged fractions are not a usable table. */
        NotPlausible,
        /** The magnets are too evenly spaced to tell one gap from another. */
        ThinMargin,
        /** The operator disarmed, or something else disarmed the car, mid-run. */
        Stopped,
    };

    /** What a completed run measured. */
    struct Result
    {
        /** The averaged gap table. */
        driver::odometer::GapTable table{};
        /** Largest disagreement between speeds, as evidence of how believable it is. */
        float spread{0.0F};
        /** Distance to the next-best rotation; what the driver must clear to phase a wheel. */
        float margin{0.0F};
        /** Which gap varied most between speeds; only meaningful with Failure::Disagreed. */
        std::uint8_t worstGap{0U};
    };

    /**
     * @brief Begin a run, discarding anything that was waiting to be confirmed.
     *
     * A pending table is cleared here rather than on a successful completion, so that what
     * the page shows always belongs to the most recent run. The cost is that a re-run which
     * stalls throws away a good measurement; that trade is recorded in ADR 0009.
     *
     * @param[in] nowMs Monotonic milliseconds.
     * @param[in] magnets Magnets on the wheel being measured.
     */
    void start(std::uint32_t nowMs, std::uint8_t magnets) noexcept;

    /**
     * @brief End a run without a table.
     *
     * @param[in] why What stopped it. Ignored if no run is in progress.
     */
    void abandon(Failure why) noexcept;

    /**
     * @brief Advance the measurement by one tick.
     *
     * @param[in] nowMs Monotonic milliseconds.
     * @param[in] armed Whether the car is still armed; a run cannot outlive its arming.
     * @param[in] motorTemperatureC Motor can temperature, or NaN when there is no sensor.
     *            A NaN runs without the overheat guard rather than refusing, which is a
     *            decision recorded in ADR 0009, not an oversight.
     * @param[in] odometer The wheel's Odometer, or nullptr if it failed to start.
     * @return Motor duty to hold, 0 to 1. Zero whenever nothing should be driving.
     */
    float update(std::uint32_t nowMs,
                 bool armed,
                 float motorTemperatureC,
                 driver::odometer::Interface* odometer) noexcept;

    /** Where the run has got to. */
    Phase phase() const noexcept { return myPhase; }

    /** Why the last run produced no table. */
    Failure failure() const noexcept { return myFailure; }

    /** Whether a run is still going, so the loop knows to keep driving it. */
    bool isRunning() const noexcept
    {
        return (myPhase == Phase::Settling) || (myPhase == Phase::Sampling);
    }

    /** Whether a table is waiting to be confirmed and stored. */
    bool hasTable() const noexcept { return myPhase == Phase::Measured; }

    /** What the last completed run measured. Only meaningful once hasTable() is true. */
    const Result& result() const noexcept { return myResult; }

    /** Which duty of the recipe is being measured, so the page can show progress. */
    std::uint8_t dutyIndex() const noexcept { return myDutyIndex; }

    /** Revolutions averaged at the current duty so far. */
    std::uint16_t samples() const noexcept { return mySamples; }

    /** One sampled revolution, kept so the caller can log the raw data behind a result. */
    struct Sample
    {
        /** Counts samples since start(); a new value means a new revolution was sampled. */
        std::uint32_t serial{0U};
        /** Which duty of the recipe it was sampled at. */
        std::uint8_t dutyIndex{0U};
        /** Pulses counted since the previous sample; a whole revolution is the magnet count. */
        std::uint32_t pulseStep{0U};
        /** Milliseconds since the previous sample. */
        std::uint32_t intervalMs{0U};
        /** The revolution's gap fractions, by slot. */
        float gaps[driver::odometer::MaxPulsesPerRevolution]{};
    };

    /** The latest sampled revolution. Its serial is 0 until the first one. */
    const Sample& lastSample() const noexcept { return myLastSample; }

    /** The averaged table of one duty; empty (count 0) until that duty is finished. */
    const driver::odometer::GapTable& measured(const std::uint8_t duty) const noexcept
    {
        return myMeasured[(duty < app::ford::calibration::DutyCount) ? duty : 0U];
    }

    /** Whether the operator has had the measured table stored. */
    bool isStored() const noexcept { return myStored; }

    /** Record that the measured table was written to NVS. */
    void markStored() noexcept { myStored = true; }

private:
    /** Average the duty just finished into the measured tables, then move on. */
    void finishDuty(std::uint32_t nowMs) noexcept;

    /** Check the measured tables against each other and produce the result, or fail. */
    void concludeRun() noexcept;

    /** Start settling at the current duty. */
    void beginSettling(std::uint32_t nowMs) noexcept;

    Phase myPhase{Phase::Idle};
    Failure myFailure{Failure::None};
    Result myResult{};
    bool myStored{false};

    /** Magnets on the wheel; set by start(). */
    std::uint8_t myMagnets{0U};

    /** Which duty of the recipe is being measured. */
    std::uint8_t myDutyIndex{0U};

    /** When the current phase began, for the settle timer. */
    std::uint32_t myPhaseStartMs{0U};

    /** Running totals for the current duty, one per gap. Double, as the `cal` command had. */
    double mySums[driver::odometer::MaxPulsesPerRevolution]{};

    /** Whole revolutions averaged into mySums so far. */
    std::uint16_t mySamples{0U};

    /** Pulse count at the last sample, to spot a whole revolution having passed. */
    std::uint32_t myLastPulses{0U};

    /** When the last revolution was sampled, for the stall timer. */
    std::uint32_t myLastSampleMs{0U};

    /** When the current duty began measuring, for the measuring timer. */
    std::uint32_t mySamplingStartMs{0U};

    /** The latest sampled revolution, for logging. */
    Sample myLastSample{};

    /** One measured table per duty of the recipe, compared against each other at the end. */
    driver::odometer::GapTable myMeasured[app::ford::calibration::DutyCount]{};
};

} // namespace app::logic
