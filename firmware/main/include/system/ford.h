/**
 * @file ford.h
 * @brief Ford's wiring, geometry and compiled-in settings, in one place.
 *
 * Everything here was duplicated between `fordLogic.cpp` and
 * `test_app/a89301_config_test.cpp`, which was harmless while those files did unrelated
 * jobs. Both now run the magnet gap calibration, and two copies of the pins and the
 * wheel would mean the two calibrations measure different wheels. See ADR 0009.
 *
 * This header holds plain values and nothing else: no driver headers, no ESP-IDF, no
 * logic. That is what lets the logic layer and a driver-level test app both include it
 * without inverting the layering.
 *
 * It is a staging post, not a resting place. The two groups below are kept apart on
 * purpose:
 *
 *   - **Hardware facts** describe how Ford is built and wired. They change when someone
 *     picks up a soldering iron, and they stay compiled in.
 *   - **Settings** are choices about how to drive it. `nvs_usage.md` has these moving to
 *     the `car` NVS namespace later; keeping them separate now means that move is a
 *     deletion from one group rather than an untangling of both.
 */

#pragma once

#include <cstdint>

namespace app::ford
{

// ---------------------------------------------------------------------------------
// Hardware facts: how Ford is built and wired.
// See documentation/design_documents/pin_mapping.md and ford_a89301_motor_controller.md.
// ---------------------------------------------------------------------------------

/** GPIO numbers. Nano pin names are in the comments; both wirings share all but SPD. */
namespace pin
{
/** D2 -> A89301 BRAKE, high = brake. Pulled up to 2V8 so a released pin brakes. */
constexpr std::uint8_t Brake{5U};
/**
 * @brief D3 <- A89301 FLT, low = fault. Wired 2026-10-07; reported, not acted on.
 *
 * Open drain, pulled up to IOREF (3V3) on the A89301 board, so no pull-up is needed here.
 * It flashes, rather than staying low, while the fault lasts - seen when the motor stalls -
 * so a fault is "low at any time recently", not one reading of the level.
 */
constexpr std::uint8_t MotorFault{6U};
/** D4 -> A89301 DIR. */
constexpr std::uint8_t Direction{7U};
/** D5 -> A89301 SPD, 20 kHz PWM. The PWM wiring only; see Scl. */
constexpr std::uint8_t Speed{8U};
/** D6 -> steering servo. */
constexpr std::uint8_t Steering{9U};
/** A3 <- drive battery divider joint. */
constexpr std::uint8_t BatteryAdc{4U};
/** D9 <- A3144 wheel sensor on the right rear wheel, active low. */
constexpr std::uint8_t Odometer{18U};
/** A0 <- TMP36 on the motor can. */
constexpr std::uint8_t MotorTempAdc{1U};
/** A1 <- TMP36 on the steering servo. */
constexpr std::uint8_t ServoTempAdc{2U};
/** A5 <- A89301 FG/SDA. I2C data, configuration wiring only. */
constexpr std::uint8_t Sda{12U};
/**
 * @brief A6 -> A89301 SPD/SCL. I2C clock, configuration wiring only.
 *
 * The A89301 has one pin for both jobs. SCL needs a pull-up and SPD a pull-down, and
 * both cannot share a wire, so the wire moves between A6 and D5 and each resistor stays
 * at its own Nano pin. Only this wire moves.
 */
constexpr std::uint8_t Scl{13U};
/**
 * @brief D7 -> Raspberry Pi header pin 10 (GPIO15 RXD), UART1 TX to the Pi.
 *
 * Not UART0 on D0/D1: that is where the boot ROM and the log print, and the Pi must not
 * receive either. 1 kOhm in series near this pin, in case only one of the two is powered.
 */
constexpr std::uint8_t PiUartTx{10U};
/** D8 <- Raspberry Pi header pin 8 (GPIO14 TXD), UART1 RX from the Pi. 1 kOhm in series at the Pi. */
constexpr std::uint8_t PiUartRx{17U};
} // namespace pin

/**
 * @brief Magnets the Hall sensor sees on the measured wheel.
 *
 * A calibration log showed that of five glued on, only two passed close enough to trigger
 * the A3144, so the wheel carried two, set opposite each other. A third, smaller one was
 * added between them on 2026-10-08, to lift the pulse rate and to make the gaps uneven.
 *
 * A magnet the A3144 does not see every pass is worse than no third magnet at all: the
 * gaps then alternate between the three- and two-magnet patterns, the phase lock breaks,
 * and the distance is wrong as well as unsteady. Check `odometer_phase_losses` stays at 0
 * before trusting a run.
 */
// Four since 2026-10-08: a fourth magnet in the half gap, off-centre towards a neighbour.
constexpr std::uint8_t OdometerMagnets{4U};

/**
 * @brief Rear wheel diameter in metres, 34 mm from ford-build.md.
 *
 * A double so that both the float the odometer driver wants and the double the config
 * app's speed estimates want come from one number.
 */
constexpr double WheelDiameterM{0.034};

/** Rear wheel circumference in metres, about 0.107 m. */
constexpr double WheelCircumferenceM{3.14159265 * WheelDiameterM};

/**
 * @brief Ford's magnet gaps as the wheel was built, as fractions of a revolution.
 *
 * Two magnets opposite each other, with the third set between them: quarter, quarter,
 * half. These are the design figures, placed by eye; GapCalibration measures what the
 * wheel actually has, and its table supersedes this one.
 *
 * Uneven at last, and that is the point. The three rotations of {0.25, 0.25, 0.5} are all
 * distinct, so the observed gaps match exactly one of them: the right rotation scores 0
 * against the other two's 0.5, a margin far above the 0.05 the matcher asks for. The
 * odometer can therefore phase-lock and time single gaps instead of whole revolutions,
 * which is what cuts the lag. See ADR 0008.
 *
 * Before 2026-10-08 the wheel had two even magnets, {0.5, 0.5}. Even gaps match every
 * rotation equally well, so the margin was 0, the odometer never locked, and
 * GapCalibration always failed. That is why it is worth having a third magnet even though
 * it is a smaller one.
 */
// The first two measured on the three-magnet wheel (0.27, 0.25); the old half gap (0.48)
// split by eye, about a third and two thirds. GapCalibration measures the real split.
constexpr float DesignGapFractions[OdometerMagnets]{0.27F, 0.25F, 0.16F, 0.32F};

/** True if DIR low drives the car forward with this motor's phase wiring. */
constexpr bool InvertMotorDirection{false};

/** Steering servo PWM frequency. */
constexpr std::uint32_t SteeringPwmFrequencyHz{50U};

/** Measured battery divider resistors (nominal 100 kOhm and 33 kOhm, 150 nF A3 to GND). */
constexpr float BatteryR1Ohm{101240.0F};
constexpr float BatteryR2Ohm{32990.0F};

// ---------------------------------------------------------------------------------
// Settings: how Ford is driven. Compiled in until they move to NVS (nvs_usage.md).
// ---------------------------------------------------------------------------------

/**
 * @brief Duty for speed command +-100.
 *
 * 1.0 (full duty) since 2026-10-06, up from 0.50, 0.40 and 0.30 earlier that day and 0.15
 * before, now that the car carries the Pi and its power bank (about 100 g more). Above the
 * highest duty driven on the floor so far: 0.20 gave 1.57 m/s and 0.15 gave 1.22 m/s
 * loaded on the first floor run (see speed_calibration below), so the top of the slider
 * is unmeasured.
 */
constexpr float TopSpeedDuty{1.0F};
/** Duty for speed command +-1: the lowest demand that starts the motor from standstill. */
constexpr float StartDuty{0.08F};
/** How long the car brakes before it drives in the other direction. */
constexpr std::uint32_t DirectionChangeBrakeMs{300U};
/**
 * @brief A89301 FLT counts as a fault for this long after it was last seen low.
 *
 * FLT flashes while a fault lasts, so the level alone reads "no fault" half the time.
 */
constexpr std::uint32_t MotorFaultHoldMs{600U};
/** No drive command for this long gives no drive; the car stays armed. */
constexpr std::uint32_t DriveTimeoutMs{500U};
/** The Pi link on UART1 (PiUartTx/PiUartRx), tested end to end at this rate. See ADR 0012. */
constexpr std::uint32_t PiUartBaudRate{921600U};
/** Battery read period; the meter averages its last 16 reads, so about 1.6 s. */
constexpr std::uint32_t BatteryReadIntervalMs{100U};
/** Motor temperature read period; the sensor averages its last 16 reads, so about 1.6 s. */
constexpr std::uint32_t MotorTempReadIntervalMs{100U};
/** Steering servo temperature read period; averaged like the motor sensor. */
constexpr std::uint32_t ServoTempReadIntervalMs{100U};

/**
 * @brief Motor can temperature at which a powered run stops.
 *
 * Temporarily lowered (60 -> 45) because the TMP36 sits on two layers of electrical
 * tape, so it reads low and late. Raise again when the sensor has direct contact with
 * the can. This threshold describes the sensor's mounting as much as the motor, which is
 * why it is a setting rather than a hardware fact.
 */
constexpr float MaxMotorTempC{45.0F};
/** Can temperature a run waits to fall below before starting again. Lowered with the above. */
constexpr float CoolMotorTempC{32.0F};

/**
 * @brief Safe mode: switch to the Disabled drive style when a temperature gets too high.
 *
 * On while Ford is a prototype. In any drive style, armed or not, the motor can or the
 * steering servo reaching SafeModeMaxTempC brakes the car and selects Disabled, which
 * reports which sensor did it. Selecting another style leaves Disabled; if the sensor is
 * still that hot, safe mode selects Disabled again at once. A missing sensor is ignored.
 */
constexpr bool SafeModeEnabled{true};
/** Motor can or steering servo temperature that triggers safe mode. */
constexpr float SafeModeMaxTempC{40.0F};

/** The magnet gap calibration recipe. See ADR 0008 for why each value is what it is. */
namespace calibration
{
/**
 * Duties the wheel is driven at, one measurement each. One speed for now, 0.10: the
 * only one at which the lifted wheel turned smoothly on 2026-10-08. At StartDuty (0.08) it
 * stuttered throughout, and at 0.15 and 0.20 it pulsed
 * (2026-10-08), which spoiled the gaps. With a single duty the cross-speed check of ADR
 * 0008 is skipped, so torque ripple is not guarded against.
 */
constexpr float Duties[]{0.10F};
/** How many duties the recipe uses. */
constexpr std::uint8_t DutyCount{static_cast<std::uint8_t>(sizeof(Duties) / sizeof(Duties[0]))};
/** Let the speed steady before measuring. */
constexpr std::uint32_t SettleMs{10000U};
/** How long to measure each duty, once settled. */
constexpr std::uint32_t MeasureMs{20000U};
/** Fewest revolutions a duty must give in MeasureMs to count. */
constexpr std::uint8_t Revolutions{20U};
/** No new revolution for this long means the wheel is not turning; give up. */
constexpr std::uint32_t StallMs{4000U};
/**
 * @brief Largest spread in one gap across the three speeds that still counts as agreement.
 *
 * Real geometry does not change with speed, so a gap that moves more than this is the
 * motor's torque ripple rather than the wheel's shape, and the table must not be stored.
 */
constexpr float Tolerance{0.01F};
} // namespace calibration

/**
 * @brief The speed calibration recipe, driven on the floor rather than on a stand.
 *
 * Every leg is LegM long and the legs alternate forward and back, so the car always
 * drives between the same two marks. A leg holds a target speed with a feed-forward duty
 * and a small PI loop on the odometer speed, then brakes early enough to stop on the
 * mark. What a leg measures - the duty that held the speed, the stopping distance -
 * becomes the starting point for the next leg, so the run learns as it goes.
 *
 * The first floor run (2026-10-05, battery 7.14 V) set the starting values: the car did
 * not start at duty 0.08, stalled at 0.10, and drove 0.82 m/s at 0.12. Below about
 * 0.8 m/s it cannot hold a speed, so the targets start there.
 */
namespace speed_calibration
{
/** Target speeds, each driven from standstill forward and back. */
constexpr float TargetsMs[]{0.8F, 1.0F, 1.2F, 1.4F};
/** How many target speeds the recipe uses. */
constexpr std::uint8_t TargetCount{static_cast<std::uint8_t>(sizeof(TargetsMs) / sizeof(TargetsMs[0]))};
/** A step between two targets: low to high forward, high to low back. Both must be targets. */
constexpr float StepLowMs{0.8F};
constexpr float StepHighMs{1.4F};
/** Two legs per target, two step legs, two lowest-speed legs. */
constexpr std::uint8_t LegCount{static_cast<std::uint8_t>((TargetCount * 2U) + 4U)};
/** Distance from start to standstill per leg, in metres. */
constexpr float LegM{5.0F};

/** Duty and speed pairs from the first floor run, for the feed-forward before anything is learned. */
constexpr float InitialDuties[]{0.12F, 0.15F, 0.18F, 0.20F};
constexpr float InitialSpeedsMs[]{0.82F, 1.22F, 1.41F, 1.57F};
constexpr std::uint8_t InitialCount{static_cast<std::uint8_t>(sizeof(InitialDuties) / sizeof(InitialDuties[0]))};

/** Lowest duty that started the car from standstill on the floor; held until the wheel turns. */
constexpr float StartDuty{0.12F};
/** Limits on the duty the speed loop may ask for. */
constexpr float MinDuty{0.05F};
constexpr float MaxDuty{0.25F};
/** Speed loop: duty per m/s of error, and duty per metre of accumulated error. */
constexpr float Kp{0.05F};
constexpr float Ki{0.10F};
/** Largest duty the integral may contribute either way. */
constexpr float IntegralLimit{0.04F};
/** Within this share of the target counts as having reached it. */
constexpr float ReachedBand{0.05F};
/** Time after reaching a target before the steady window opens, or a step is taken. */
constexpr std::uint32_t HoldMs{500U};
/** Fewest pulses in the steady window for its speed and duty to count. */
constexpr std::uint32_t MinMeasuredPulses{6U};

/** Stopping distance is modelled as k * v^2; this k comes from the first floor run. */
constexpr float InitialStopK{0.25F};

/** Lowest-speed legs: hold StartDuty this long after the wheel turns, then step down. */
constexpr std::uint32_t MinimumSettleMs{1000U};
/** Duties stepped through, one per MinimumStepMs, until the wheel stalls. */
constexpr float MinimumDuties[]{0.10F, 0.09F, 0.08F, 0.07F, 0.06F, 0.05F};
constexpr std::uint8_t MinimumCount{static_cast<std::uint8_t>(sizeof(MinimumDuties) / sizeof(MinimumDuties[0]))};
constexpr std::uint32_t MinimumStepMs{1000U};

/** No pulse this long after a leg starts: the motor did not start. Start-up takes about 1.1 s. */
constexpr std::uint32_t StartTimeoutMs{2500U};
/** No pulse this long once the wheel has moved: it stopped. */
constexpr std::uint32_t StallMs{1000U};
/** Longest a leg may drive. */
constexpr std::uint32_t MaxLegMs{15000U};
/** Brake time after each leg, so the car stands still before it drives the other way. */
constexpr std::uint32_t BrakeMs{1500U};
} // namespace speed_calibration

} // namespace app::ford
