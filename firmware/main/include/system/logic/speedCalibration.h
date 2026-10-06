/**
 * @file speedCalibration.h
 * @brief The speed calibration, as a state machine driven one tick at a time.
 *
 * Like GapCalibration, this knows nothing about ESP-IDF, FreeRTOS or MQTT: it is handed
 * the time, whether the car is armed, a motor temperature and an Odometer, and it answers
 * with the signed duty to hold and whether to brake. That keeps it testable on the host
 * through the odometer stub, and lets the shared loop keep servicing the heartbeat.
 *
 * It runs with the car on the floor. Every leg ends LegM from where it started, braking
 * included, so the car shuttles between the same two marks.
 */

#pragma once

#include <cstdint>
#include <limits>

#include "system/ford.h"

namespace driver::odometer { class Interface; }

namespace app::logic
{

/**
 * @brief Learns what it takes to drive Ford at a given speed, and to stop on a mark.
 *
 * Three kinds of leg, all LegM long:
 *
 *   - **Target**: from standstill to a target speed, held with a feed-forward duty and a
 *     PI loop on the odometer speed. Measures the steady duty, the rise time and the
 *     overshoot.
 *   - **Step**: as Target, but the target changes once the first one has been held, to
 *     measure how the car responds from one speed to another.
 *   - **Minimum**: no speed loop. The duty steps down once a second until the wheel
 *     stalls, to find the lowest duty that keeps the car rolling.
 *
 * Every leg brakes when the distance left equals the predicted stopping distance, k * v^2.
 * After each brake k is updated from what the car actually needed, and each steady duty
 * replaces the feed-forward for its target. Both are kept across runs until a restart.
 */
class SpeedCalibration final
{
public:
    /** Where a run has got to. */
    enum class Phase : std::uint8_t
    {
        /** Nothing is running and nothing has run since boot. */
        Idle,
        /** Driving a leg. */
        Driving,
        /** Braking after a leg, before the next one or the end. */
        Braking,
        /** Every leg has been driven. Each leg's own outcome says how it went. */
        Finished,
        /** The run was ended early. See failure(). */
        Failed,
    };

    /** Why a run ended early. A leg that went wrong does not end the run. */
    enum class Failure : std::uint8_t
    {
        None,
        /** No Odometer, so there is nothing to measure with. */
        NoOdometer,
        /** The motor can went above its limit. */
        TooHot,
        /** The operator disarmed, or something else disarmed the car, mid-run. */
        Stopped,
    };

    /** How one leg went. */
    enum class Outcome : std::uint8_t
    {
        /** Not driven yet. */
        None,
        /** Target or Step: the target was held and the steady speed and duty measured. */
        Reached,
        /** Target or Step: the target was reached, but too briefly to measure it steadily. */
        Short,
        /** Target or Step: the target was never reached before the car had to brake. */
        NotReached,
        /** Minimum: the wheel stalled at a step; duty is the lowest that still rolled. */
        Lowest,
        /** Minimum: every step still rolled; the lowest is below the list. */
        Bottom,
        /** The wheel never turned. */
        NoStart,
        /** The wheel turned and then stopped, before anything was measured. */
        Stalled,
    };

    /** What a leg is asked to do. */
    struct Plan
    {
        /** Speed held first on a Step leg; 0 for every other leg. */
        float fromMs{0.0F};
        /** Speed held last; 0 on a Minimum leg. */
        float targetMs{0.0F};
        bool forward{true};
        bool minimum{false};
    };

    /** One driven leg. */
    struct Leg
    {
        /** Position in the recipe, 0 to LegCount - 1. */
        std::uint8_t index{0U};
        Plan plan{};
        Outcome outcome{Outcome::None};
        /**
         * @brief Steady speed in m/s, timed over whole revolutions.
         *
         * Minimum: the speed at the lowest duty that rolled. NotReached: the speed when it
         * braked. NaN when there is none.
         */
        float speedMs{std::numeric_limits<float>::quiet_NaN()};
        /** Mean duty over the steady window; Minimum: the lowest duty that rolled. NaN if none. */
        float duty{std::numeric_limits<float>::quiet_NaN()};
        /** Time from the wheel turning (or the step) to within ReachedBand of the target. */
        float riseS{std::numeric_limits<float>::quiet_NaN()};
        /** Furthest the speed went past the target, in m/s; NaN if never reached. */
        float overshootMs{std::numeric_limits<float>::quiet_NaN()};
        /** Speed when the brake went on. */
        float brakeSpeedMs{0.0F};
        /** Distance from the brake going on to standing still. */
        float stopM{0.0F};
        /** Distance from the start of the leg to standing still. */
        float distanceM{0.0F};
    };

    SpeedCalibration() noexcept;

    /**
     * @brief Begin a run, forgetting the previous run's legs but not what it learned.
     *
     * @param[in] nowMs Monotonic milliseconds.
     * @param[in] magnets Magnets on the measured wheel.
     * @param[in] circumferenceM Distance the wheel rolls in one revolution.
     */
    void start(std::uint32_t nowMs, std::uint8_t magnets, float circumferenceM) noexcept;

    /**
     * @brief End a run early.
     *
     * @param[in] why What stopped it. Ignored if no run is in progress.
     */
    void abandon(Failure why) noexcept;

    /**
     * @brief Advance the run by one tick.
     *
     * @param[in] nowMs Monotonic milliseconds.
     * @param[in] armed Whether the car is still armed; a run cannot outlive its arming.
     * @param[in] motorTemperatureC Motor can temperature, or NaN when there is no sensor.
     * @param[in] odometer The wheel's Odometer, or nullptr if it failed to start.
     * @return Signed motor duty to hold: positive forward, negative backward, 0 for none.
     *         While isBraking() it is 0 and the caller should brake rather than coast.
     */
    float update(std::uint32_t nowMs,
                 bool armed,
                 float motorTemperatureC,
                 driver::odometer::Interface* odometer) noexcept;

    Phase phase() const noexcept { return myPhase; }
    Failure failure() const noexcept { return myFailure; }

    /** Whether a run is still going, so the loop knows to keep driving it. */
    bool isRunning() const noexcept
    {
        return (myPhase == Phase::Driving) || (myPhase == Phase::Braking);
    }

    /** Whether the car should hold the brake now. */
    bool isBraking() const noexcept { return myPhase == Phase::Braking; }

    /** The leg being driven or braked, or LegCount once the run is over. */
    std::uint8_t legIndex() const noexcept { return myLegIndex; }

    /** What a leg of the recipe is asked to do. */
    static Plan planOf(std::uint8_t leg) noexcept;

    /** Whether any leg of this run has finished, so lastLeg() means something. */
    bool hasLastLeg() const noexcept { return myHasLastLeg; }

    /** The most recently finished leg. */
    const Leg& lastLeg() const noexcept { return myLastLeg; }

    /** The stopping distance factor k in k * v^2, as learned so far. */
    float stopK() const noexcept { return myStopK; }

    /** The feed-forward duty for a speed and direction, as learned so far. */
    float feedForward(float speedMs, bool forward) const noexcept;

private:
    /** Start driving the current leg. */
    void beginLeg(std::uint32_t nowMs, std::uint32_t pulses) noexcept;

    /** Stop driving: work out what the leg measured and start braking. */
    void endLeg(std::uint32_t nowMs, std::uint32_t pulses, float speedMs, Outcome outcome) noexcept;

    /** Braking is over: record the distances, learn, and move on. */
    void finishLeg(std::uint32_t nowMs, std::uint32_t pulses) noexcept;

    /** The duty a Target or Step leg asks for this tick. */
    float targetDuty(std::uint32_t nowMs, float speedMs) noexcept;

    /** The duty a Minimum leg asks for this tick; may end the leg. */
    float minimumDuty(std::uint32_t nowMs, std::uint32_t pulses, bool edge) noexcept;

    /** Metres per pulse; whole revolutions are exact, single pulses close enough here. */
    float metersPerPulse() const noexcept;

    /** Index of a speed in TargetsMs, or TargetCount if it is not one. */
    static std::uint8_t targetIndexOf(float speedMs) noexcept;

    Phase myPhase{Phase::Idle};
    Failure myFailure{Failure::None};

    std::uint8_t myMagnets{0U};
    float myCircumferenceM{0.0F};

    // Learned, and kept across runs until a restart.
    float myStopK{app::ford::speed_calibration::InitialStopK};
    float myFeedForward[2][app::ford::speed_calibration::TargetCount]{};

    std::uint8_t myLegIndex{0U};
    Plan myPlan{};
    std::uint32_t myLegStartMs{0U};
    std::uint32_t myLegStartPulses{0U};
    /** start() has no odometer, so the first update() takes the starting pulse count. */
    bool myLegStartPending{false};
    std::uint32_t myLastTickMs{0U};

    // Pulse edges.
    std::uint32_t myLastPulses{0U};
    std::uint32_t myLastEdgeMs{0U};
    bool myMoved{false};
    std::uint32_t myFirstEdgeMs{0U};

    // Target and Step legs.
    float myIntegral{0.0F};
    float myLastDuty{0.0F};
    /** Step legs: whether the second target is now the one being held. */
    bool myStepped{false};
    /** When the speed to measure the rise from began: the first pulse, or the step. */
    std::uint32_t myRiseFromMs{0U};
    bool myReached{false};
    std::uint32_t myReachedMs{0U};
    /** Whether the final target is being held, so the window and overshoot apply. */
    bool myFinalReached{false};
    float myOvershootMs{0.0F};

    // The steady window, opened HoldMs after the final target is reached.
    bool myWindowStarted{false};
    std::uint32_t myWindowStartPulses{0U};
    std::uint32_t myWindowStartMs{0U};
    std::uint32_t myWindowEndPulses{0U};
    std::uint32_t myWindowEndMs{0U};
    float myDutySum{0.0F};
    std::uint32_t myDutyMs{0U};

    // Minimum legs.
    /** -1 while still at StartDuty; otherwise the index into MinimumDuties. */
    std::int8_t myMinimumStep{-1};
    std::uint32_t myMinimumStepMs{0U};
    /** Edges in the second half of the current step, timing its speed. */
    bool myStepHasEdge{false};
    std::uint32_t myStepFirstPulses{0U};
    std::uint32_t myStepFirstMs{0U};
    std::uint32_t myStepLastPulses{0U};
    std::uint32_t myStepLastMs{0U};
    float myLowestDuty{std::numeric_limits<float>::quiet_NaN()};
    float myLowestSpeedMs{std::numeric_limits<float>::quiet_NaN()};

    // Braking.
    std::uint32_t myBrakeStartMs{0U};
    std::uint32_t myBrakePulses{0U};

    /** The leg being driven, filled in as it goes. */
    Leg myLeg{};

    bool myHasLastLeg{false};
    Leg myLastLeg{};
};

} // namespace app::logic
