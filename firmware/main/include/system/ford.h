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
/** A4 <- A89301 FG/SDA. I2C data, configuration wiring only. */
constexpr std::uint8_t Sda{11U};
/**
 * @brief A5 -> A89301 SPD/SCL. I2C clock, configuration wiring only.
 *
 * The A89301 has one pin for both jobs. SCL needs a pull-up and SPD a pull-down, and
 * both cannot share a wire, so the wire moves between A5 and D5 and each resistor stays
 * at its own Nano pin. Only this wire moves.
 */
constexpr std::uint8_t Scl{12U};
} // namespace pin

/** Magnets fitted to the measured wheel. */
constexpr std::uint8_t OdometerMagnets{6U};

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
 * Two larger magnets sit at the midpoints of the two gaps either side of one original,
 * so four gaps are an eighth of a turn and two are a quarter. Treating them as equal
 * sixths instead would make a per-gap speed wrong by a third; these design values get
 * that down to roughly a tenth with no calibration at all, which is why they are worth
 * compiling in. Used until a calibration session measures the real ones. See ADR 0008.
 */
constexpr float DesignGapFractions[OdometerMagnets]{
    0.125F, 0.125F, 0.25F, 0.25F, 0.125F, 0.125F};

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

/** Duty for speed command +-100: about 1.5 m/s unloaded. Raise only after the load test. */
constexpr float TopSpeedDuty{0.15F};
/** Duty for speed command +-1: the lowest demand that starts the motor from standstill. */
constexpr float StartDuty{0.08F};
/** How long the car brakes before it drives in the other direction. */
constexpr std::uint32_t DirectionChangeBrakeMs{300U};
/** No drive command for this long gives no drive; the car stays armed. */
constexpr std::uint32_t DriveTimeoutMs{500U};
/** Battery read period; the meter averages its last 16 reads, so about 1.6 s. */
constexpr std::uint32_t BatteryReadIntervalMs{100U};
/** Motor temperature read period; the sensor averages its last 16 reads, so about 1.6 s. */
constexpr std::uint32_t MotorTempReadIntervalMs{100U};

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

/** The magnet gap calibration recipe. See ADR 0008 for why each value is what it is. */
namespace calibration
{
/** Duties the wheel is driven at, one measurement each. Three speeds, so they can disagree. */
constexpr float Duties[]{0.10F, 0.15F, 0.20F};
/** How many duties the recipe uses. */
constexpr std::uint8_t DutyCount{static_cast<std::uint8_t>(sizeof(Duties) / sizeof(Duties[0]))};
/** Let the speed steady before measuring. */
constexpr std::uint32_t SettleMs{2000U};
/** Revolutions averaged per duty. */
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

} // namespace app::ford
