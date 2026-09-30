/**
 * @file a89301_programmer.h
 * @brief I2C register and EEPROM access for the A89301 BLDC controller.
 *
 * Register numbers, fields and the EEPROM procedure follow the Allegro A89301 datasheet
 * (I2C Operation and EEPROM Map, Programming EEPROM).
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c/interface.h"

namespace driver::motor::a89301
{

/** 7-bit I2C address of the A89301. */
inline constexpr std::uint8_t I2cAddress{0x55U};

/** Offset from an EEPROM address to its working register (register = EEPROM address + 64). */
inline constexpr std::uint8_t WorkingRegisterOffset{64U};

/** First user EEPROM address. */
inline constexpr std::uint8_t EepromFirst{8U};

/** Last user EEPROM address. */
inline constexpr std::uint8_t EepromLast{22U};

/** Largest I2C speed demand value (0 - 511 represents 0 - 100 %). */
inline constexpr std::uint16_t SpeedDemandMax{511U};

/**
 * @brief Read-only status registers.
 */
namespace readback
{
inline constexpr std::uint8_t MotorSpeed{120U};     /**< Motor speed, Hz = value * 0.530. */
inline constexpr std::uint8_t BusCurrent{121U};     /**< Bus current, mA = value / (SENSE_RESISTOR / 125). */
inline constexpr std::uint8_t QCurrent{122U};       /**< Q-axis current, same scale as bus current. */
inline constexpr std::uint8_t Vbb{123U};            /**< Supply voltage, V = value / 5. */
inline constexpr std::uint8_t Temperature{124U};    /**< Temperature, see datasheet. */
inline constexpr std::uint8_t ControlDemand{125U};  /**< Speed demand, 0 - 511 represents 0 - 100 %. */
inline constexpr std::uint8_t ControlCommand{126U}; /**< Control command, 0 - 511 represents 0 - 100 %. */
inline constexpr std::uint8_t OperationState{127U}; /**< Operation state in bits [15:12]. */
} // namespace readback

/**
 * @brief Description of one documented EEPROM/register field.
 */
struct Field
{
    /** Field name as written in the datasheet. */
    const char* name;

    /** EEPROM address (8 - 22). The working register is address + 64. */
    std::uint8_t address;

    /** Position of the least significant bit. */
    std::uint8_t shift;

    /** Number of bits. */
    std::uint8_t width;

    /** Short description of the values. */
    const char* note;
};

/**
 * @brief Documented user fields (datasheet Table 2).
 *
 * Bits that are not listed here are kept unchanged by writeField().
 */
inline constexpr Field Fields[]{
    {"RATED_SPEED", 8U, 0U, 11U, "Hz = value * 0.530 (electrical)"},
    {"SPEED_CLOSE_LOOP", 8U, 11U, 1U, "1: closed loop, 0: open loop"},
    {"CLOCK_PWM", 8U, 12U, 1U, "1: clock mode, 0: PWM mode"},
    {"ACCELERATE_RANGE", 8U, 13U, 1U, "0: k = 0.05, 1: k = 3.2 (see ACCELERATION)"},
    {"DIRECTION", 8U, 14U, 1U, "1: ABC, 0: ACB"},
    {"PWMIN_RANGE", 8U, 15U, 1U, "1: PWM <= 2.8 kHz, 0: PWM > 2.8 kHz"},
    {"ACCELERATION", 9U, 0U, 8U, "Hz/s = value * k"},
    {"MOTOR_RESISTANCE", 9U, 8U, 8U, "scaled, see datasheet"},
    {"RATED_CURRENT", 10U, 0U, 11U, "mA = value / (SENSE_RESISTOR / 125)"},
    {"SPD_MODE", 10U, 11U, 1U, "1: analog, 0: digital (PWM or clock)"},
    {"STARTUP_CURRENT", 10U, 13U, 3U, "0: NA, else rated current * (value + 1) / 8"},
    {"OPEN_DRIVE", 11U, 3U, 1U, "see application note"},
    {"DIRECT_DR_ANGLE", 11U, 5U, 1U, "1: MOTOR_INDUCTANCE sets phase advance in degrees"},
    {"MAX_START_CURR", 11U, 6U, 1U, "see application note"},
    {"POWER_CTRL_EN", 11U, 7U, 1U, "1: enable the current limit"},
    {"STARTUP_MODE", 11U, 10U, 2U, "0: 6 pulse, 1: 2 pulse, 2: slight move, 3: align & go"},
    {"WAIT_STATIONARY", 11U, 12U, 1U, "see application note"},
    {"EXTEND_LOCK_MASK", 11U, 14U, 1U, "see application note"},
    {"PID_P", 12U, 0U, 8U, "position observer P gain"},
    {"MOTOR_INDUCTANCE", 12U, 8U, 5U, "see application note"},
    {"OVER_SPEED_LOCK", 12U, 13U, 1U, "see application note"},
    {"OPEN_WINDOW", 12U, 15U, 1U, "1: open window for inductance tuning, 0: normal"},
    {"PID_I", 13U, 0U, 8U, "position observer I gain"},
    {"DELAY_START", 13U, 14U, 1U, "1: delayed start, 0: start right after windmill check"},
    {"FG_PIN_DIS", 14U, 4U, 1U, "1: FG pin always high (for I2C)"},
    {"ANGLE_ERROR_LOCK", 15U, 2U, 2U, "startup lock detect: 0: off, 1: 5, 2: 9, 3: 13 degrees"},
    {"SOFT_OFF", 15U, 6U, 1U, "soft off enable"},
    {"SOFT_ON", 15U, 7U, 1U, "soft on enable"},
    {"BEMF_LOCK_FILTER", 16U, 12U, 2U, "see application note"},
    {"RATED_VOLTAGE", 20U, 0U, 8U, "V = value / 5"},
    {"SENSE_RESISTOR", 20U, 8U, 8U, "mOhm = value / 3.7"},
    {"SPEED_INPUT_OFF_THRESHOLD", 21U, 8U, 2U, "0: 10 %, 1: 6 %, 2: 15 %, 3: 20 %"},
    {"STANDBY_DIS", 21U, 15U, 1U, "0: standby enabled, 1: standby disabled"},
    {"SPEED_RESPONSE_TC", 22U, 0U, 6U, "closed loop time constant s = value / 15"},
    {"RESTART_ATTEMPT", 22U, 6U, 2U, "0: always, 1: 3 times, 2: 5 times, 3: 10 times"},
    {"BRAKE_MODE", 22U, 8U, 1U, "0: brake when safe, 1: 100 % uncontrolled"},
    {"SOFT_OFF_TIME", 22U, 9U, 1U, "max soft off time, 0: 1 s, 1: 4 s"},
    {"VIBRATION_LOCK", 22U, 10U, 1U, "see application note"},
    {"LOCK_RESTART_SET", 22U, 11U, 1U, "restart delay after lock, 0: 5 s, 1: 10 s"},
};

/**
 * @brief Allegro-only bits of one EEPROM word that must always keep their default value.
 */
struct FixedBits
{
    /** EEPROM address. */
    std::uint8_t address;

    /** Bits that are Allegro-only. */
    std::uint16_t mask;

    /** Required value of the Allegro-only bits. */
    std::uint16_t value;
};

/**
 * @brief Allegro-only bits (application note UM-A89301, "Default values of Allegro-only bits").
 *
 * Words 8, 9 and 20 have no Allegro-only bits.
 */
inline constexpr FixedBits AllegroOnlyBits[]{
    {10U, 0x1000U, 0x0000U},
    {11U, 0xA317U, 0x8000U},
    {12U, 0x4000U, 0x0000U},
    {13U, 0xBF00U, 0x0100U},
    {14U, 0xFFEFU, 0x0E05U},
    {15U, 0x3033U, 0x3031U},
    {16U, 0xC000U, 0x0000U},
    {17U, 0xFC00U, 0x2000U},
    {18U, 0x00FFU, 0x000DU},
    {19U, 0x00FFU, 0x0000U},
    {21U, 0x7C1FU, 0x4000U},
    {22U, 0x7000U, 0x1000U},
};

/**
 * @brief Check that the Allegro-only bits of an EEPROM word have their default values.
 *
 * @param[in] address EEPROM address.
 * @param[in] word Word to check.
 * @return True if all Allegro-only bits of the word are default.
 */
bool hasDefaultAllegroBits(std::uint8_t address, std::uint16_t word) noexcept;

/**
 * @brief Name of an operation state (register 127 bits [15:12], application note UM-A89301).
 *
 * @param[in] state Operation state 0 - 15.
 * @return Short state name.
 */
const char* stateName(std::uint8_t state) noexcept;

/**
 * @brief Find a documented field by name.
 *
 * @param[in] name Field name, case sensitive.
 * @return Pointer to the field, or nullptr if not found.
 */
const Field* findField(const char* name) noexcept;

/**
 * @brief Extract a field value from a register word.
 *
 * @param[in] field Field to extract.
 * @param[in] word Register word.
 * @return Field value.
 */
std::uint16_t fieldValue(const Field& field, std::uint16_t word) noexcept;

/**
 * @brief Check if an EEPROM address may be programmed by Programmer::programEeprom().
 *
 * Addresses outside 8 - 22 are not user settings. Address 19 is factory controlled.
 * Address 17 holds the I2C speed control and is never saved.
 *
 * @param[in] address EEPROM address.
 * @return True if the address may be programmed.
 */
bool isEepromWritable(std::uint8_t address) noexcept;

/**
 * @brief Function used to wait during EEPROM programming.
 *
 * @param[in] ms Time to wait in milliseconds.
 */
using DelayFunction = void (*)(std::uint32_t ms);

/**
 * @brief I2C register and EEPROM access for the A89301.
 *
 * Working registers take effect immediately and are reloaded from EEPROM on power-up,
 * so changes made with writeField() are temporary until programEeprom() is used.
 */
class Programmer
{
public:
    /**
     * @brief Constructor.
     *
     * @param[in] i2c Initialized I2C bus connected to SPD/SCL and FG/SDA.
     * @param[in] delay Function used to wait for EEPROM programming.
     *
     * The referenced I2C driver must outlive this instance.
     */
    Programmer(driver::i2c::Interface& i2c, DelayFunction delay) noexcept;

    /**
     * @brief Check if the A89301 answers on the bus.
     *
     * @return True if the device acknowledged its address.
     */
    bool isPresent() noexcept;

    /**
     * @brief Read any 16-bit register.
     *
     * Registers 0 - 22 return EEPROM contents, 72 - 86 the working registers.
     *
     * @param[in] reg Register address.
     * @param[out] value Register value.
     * @return True on success, false otherwise.
     */
    bool readRegister(std::uint8_t reg, std::uint16_t& value) noexcept;

    /**
     * @brief Read a field from the EEPROM or from the working register.
     *
     * @param[in] field Field to read.
     * @param[in] working True to read the working register, false to read the EEPROM.
     * @param[out] value Field value.
     * @return True on success, false otherwise.
     */
    bool readField(const Field& field, bool working, std::uint16_t& value) noexcept;

    /**
     * @brief Change a field in the working register (temporary, not saved to EEPROM).
     *
     * The other bits of the register are kept unchanged. Fields that overlap Allegro-only bits are rejected.
     *
     * @param[in] field Field to write.
     * @param[in] value New field value.
     * @return True if written and read back correctly, false otherwise.
     */
    bool writeField(const Field& field, std::uint16_t value) noexcept;

    /**
     * @brief Control the speed over I2C instead of the SPD pin.
     *
     * @param[in] demand Speed demand 0 - 511 (0 - 100 %).
     * @return True on success, false otherwise.
     */
    bool setSpeedDemand(std::uint16_t demand) noexcept;

    /**
     * @brief Give speed control back to the SPD pin and clear the I2C demand.
     *
     * @return True on success, false otherwise.
     */
    bool releaseSpeedDemand() noexcept;

    /**
     * @brief Erase and program one EEPROM word, then read it back.
     *
     * @attention The supply must stay on during programming (about 30 ms per word).
     *
     * Values with non-default Allegro-only bits are rejected, see hasDefaultAllegroBits().
     *
     * @param[in] address EEPROM address, see isEepromWritable().
     * @param[in] value Value to program.
     * @return True if the word was programmed and verified, false otherwise.
     */
    bool programEeprom(std::uint8_t address, std::uint16_t value) noexcept;

    Programmer(const Programmer&)            = delete;
    Programmer& operator=(const Programmer&) = delete;
    Programmer(Programmer&&)                 = delete;
    Programmer& operator=(Programmer&&)      = delete;

private:
    /**
     * @brief Write any 16-bit register.
     *
     * @param[in] reg Register address.
     * @param[in] value Value to write.
     * @return True on success, false otherwise.
     */
    bool writeRegister(std::uint8_t reg, std::uint16_t value) noexcept;

    /** I2C bus connected to the A89301. */
    driver::i2c::Interface& myI2c;

    /** Function used to wait during EEPROM programming. */
    DelayFunction myDelay;
};

} // namespace driver::motor::a89301
