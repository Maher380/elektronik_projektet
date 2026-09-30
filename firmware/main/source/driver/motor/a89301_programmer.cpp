/**
 * @file a89301_programmer.cpp
 * @brief I2C register and EEPROM access for the A89301 BLDC controller.
 */

#include "driver/motor/a89301_programmer.h"

#include <cstring>

namespace driver::motor::a89301
{
namespace
{
/** EEPROM control register: bit 0 EN (high voltage), 1 ER (erase), 2 WR (write), 3 RD (read). */
constexpr std::uint8_t EepromControl{161U};

/** EEPROM address register. */
constexpr std::uint8_t EepromAddress{162U};

/** EEPROM data-in register. */
constexpr std::uint8_t EepromDataIn{163U};

/** Control value for erase with high voltage (ER | EN). */
constexpr std::uint16_t EepromErase{0x0003U};

/** Control value for write with high voltage (WR | EN). */
constexpr std::uint16_t EepromWrite{0x0005U};

/** Control value for idle. */
constexpr std::uint16_t EepromIdle{0x0000U};

/** Time for one erase or write pulse in milliseconds (datasheet: 15 ms). */
constexpr std::uint32_t EepromPulseMs{15U};

/** Working register holding I2C_SPEED_MODE [9] and SPEED_DEMAND [8:0]. */
constexpr std::uint8_t SpeedControlRegister{17U + WorkingRegisterOffset};

/** I2C_SPEED_MODE bit in the speed control register. */
constexpr std::uint16_t I2cSpeedModeBit{1U << 9U};

/** SPEED_DEMAND bits in the speed control register. */
constexpr std::uint16_t SpeedDemandMask{0x01FFU};

/**
 * @brief Create the bit mask of a field.
 *
 * @param[in] field Field to create the mask for.
 * @return Mask aligned to the field position.
 */
std::uint16_t fieldMask(const Field& field) noexcept
{
    return static_cast<std::uint16_t>(((1UL << field.width) - 1UL) << field.shift);
}

/**
 * @brief Find the Allegro-only bits of an EEPROM word.
 *
 * @param[in] address EEPROM address.
 * @return Pointer to the Allegro-only bits, or nullptr if the word has none.
 */
const FixedBits* allegroOnlyBits(const std::uint8_t address) noexcept
{
    for (const auto& fixed : AllegroOnlyBits)
    {
        if (fixed.address == address) { return &fixed; }
    }
    return nullptr;
}
} // namespace

bool hasDefaultAllegroBits(const std::uint8_t address, const std::uint16_t word) noexcept
{
    const FixedBits* fixed{allegroOnlyBits(address)};
    return (fixed == nullptr) || ((word & fixed->mask) == fixed->value);
}

const char* stateName(const std::uint8_t state) noexcept
{
    constexpr const char* names[16]{
        "idle", "first cycle", "IPD", "spinning", "n/a", "LOCK", "brake", "sleep",
        "state 8", "windmill", "chg dir", "OCP", "OTP", "bad system", "brake pin", "soft off",
    };
    return (state < 16U) ? names[state] : "?";
}

const Field* findField(const char* name) noexcept
{
    if (name == nullptr) { return nullptr; }

    for (const auto& field : Fields)
    {
        if (std::strcmp(field.name, name) == 0) { return &field; }
    }
    return nullptr;
}

std::uint16_t fieldValue(const Field& field, const std::uint16_t word) noexcept
{
    return static_cast<std::uint16_t>((word & fieldMask(field)) >> field.shift);
}

bool isEepromWritable(const std::uint8_t address) noexcept
{
    return (address >= EepromFirst) && (address <= EepromLast) && (address != 17U) && (address != 19U);
}

Programmer::Programmer(driver::i2c::Interface& i2c, const DelayFunction delay) noexcept
    : myI2c{i2c}
    , myDelay{delay}
{}

bool Programmer::isPresent() noexcept
{
    return myI2c.probe(I2cAddress);
}

bool Programmer::readRegister(const std::uint8_t reg, std::uint16_t& value) noexcept
{
    // Two step read: write the register address, then read two bytes (MSB first).
    std::uint8_t data[2]{reg, 0U};
    if (!myI2c.write(I2cAddress, data, 1U)) { return false; }
    if (!myI2c.read(I2cAddress, data, 2U)) { return false; }

    value = static_cast<std::uint16_t>((data[0] << 8U) | data[1]);
    return true;
}

bool Programmer::writeRegister(const std::uint8_t reg, const std::uint16_t value) noexcept
{
    const std::uint8_t data[3]{reg,
                               static_cast<std::uint8_t>(value >> 8U),
                               static_cast<std::uint8_t>(value & 0xFFU)};
    return myI2c.write(I2cAddress, data, sizeof(data));
}

bool Programmer::readField(const Field& field, const bool working, std::uint16_t& value) noexcept
{
    const std::uint8_t reg{static_cast<std::uint8_t>(working ? field.address + WorkingRegisterOffset
                                                             : field.address)};
    std::uint16_t word{};
    if (!readRegister(reg, word)) { return false; }

    value = fieldValue(field, word);
    return true;
}

bool Programmer::writeField(const Field& field, const std::uint16_t value) noexcept
{
    const std::uint16_t mask{fieldMask(field)};
    if ((static_cast<std::uint32_t>(value) << field.shift) & ~static_cast<std::uint32_t>(mask)) { return false; }

    // Never touch Allegro-only bits.
    const FixedBits* fixed{allegroOnlyBits(field.address)};
    if ((fixed != nullptr) && ((fixed->mask & mask) != 0U)) { return false; }

    const std::uint8_t reg{static_cast<std::uint8_t>(field.address + WorkingRegisterOffset)};
    std::uint16_t word{};
    if (!readRegister(reg, word)) { return false; }

    const std::uint16_t newWord{static_cast<std::uint16_t>((word & ~mask) | (value << field.shift))};
    if (!writeRegister(reg, newWord)) { return false; }

    std::uint16_t readBack{};
    return readRegister(reg, readBack) && (readBack == newWord);
}

bool Programmer::setSpeedDemand(const std::uint16_t demand) noexcept
{
    if (demand > SpeedDemandMax) { return false; }

    std::uint16_t word{};
    if (!readRegister(SpeedControlRegister, word)) { return false; }

    word = static_cast<std::uint16_t>((word & ~SpeedDemandMask) | I2cSpeedModeBit | demand);
    return writeRegister(SpeedControlRegister, word);
}

bool Programmer::releaseSpeedDemand() noexcept
{
    std::uint16_t word{};
    if (!readRegister(SpeedControlRegister, word)) { return false; }

    word = static_cast<std::uint16_t>(word & ~(SpeedDemandMask | I2cSpeedModeBit));
    return writeRegister(SpeedControlRegister, word);
}

bool Programmer::programEeprom(const std::uint8_t address, const std::uint16_t value) noexcept
{
    if (!isEepromWritable(address) || !hasDefaultAllegroBits(address, value) || (myDelay == nullptr)) { return false; }

    // Datasheet procedure: a word must be erased before it is written.
    const bool erased{writeRegister(EepromAddress, address)
                      && writeRegister(EepromDataIn, 0U)
                      && writeRegister(EepromControl, EepromErase)};
    if (erased) { myDelay(EepromPulseMs); }

    const bool written{erased
                       && writeRegister(EepromAddress, address)
                       && writeRegister(EepromDataIn, value)
                       && writeRegister(EepromControl, EepromWrite)};
    if (written) { myDelay(EepromPulseMs); }

    // Always leave the EEPROM control idle, also after a failed step.
    const bool idle{writeRegister(EepromControl, EepromIdle)};

    std::uint16_t readBack{};
    return written && idle && readRegister(address, readBack) && (readBack == value);
}

} // namespace driver::motor::a89301
