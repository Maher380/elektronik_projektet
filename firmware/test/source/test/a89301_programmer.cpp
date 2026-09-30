#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "driver/i2c/stub.h"
#include "driver/motor/a89301_programmer.h"
#include "test/a89301_programmer.h"

namespace
{
namespace a89301 = driver::motor::a89301;

std::uint32_t totalDelayMs{0U};

void fakeDelay(const std::uint32_t ms) noexcept
{
    totalDelayMs += ms;
}

bool expect(bool condition, const char* message) noexcept
{
    if (!condition)
    {
        std::printf("A89301 programmer test failed: %s\n", message);
        return false;
    }

    return true;
}
} // namespace

namespace test
{
bool runA89301ProgrammerTest() noexcept
{
    driver::i2c::Stub i2c{a89301::I2cAddress};
    a89301::Programmer programmer{i2c, fakeDelay};

    // No documented field may overlap Allegro-only bits.
    for (const auto& field : a89301::Fields)
    {
        const auto fieldBits{static_cast<std::uint16_t>(((1UL << field.width) - 1UL) << field.shift)};
        for (const auto& fixed : a89301::AllegroOnlyBits)
        {
            if ((fixed.address == field.address) && ((fixed.mask & fieldBits) != 0U))
            {
                std::printf("Field %s overlaps Allegro-only bits\n", field.name);
                return false;
            }
        }
    }

    // A known good configuration (read from the ford A89301) has default Allegro-only bits.
    constexpr std::uint16_t goodConfig[]{0x275E, 0x7120, 0x00DC, 0xD880, 0x0028, 0x011E, 0x0E15, 0x39B1,
                                         0x0A6C, 0x2000, 0x8E0D, 0x5500, 0x0B25, 0xC020, 0x925E};
    for (std::uint8_t i{0U}; i < 15U; ++i)
    {
        const auto address{static_cast<std::uint8_t>(a89301::EepromFirst + i)};
        if (!expect(a89301::hasDefaultAllegroBits(address, goodConfig[i]), "known good word should pass")) { return false; }
    }
    if (!expect(!a89301::hasDefaultAllegroBits(13U, 0x211EU), "changed Allegro-only bit should be detected")) { return false; }
    if (!expect(std::strcmp(a89301::stateName(3U), "spinning") == 0, "state name")) { return false; }

    if (!expect(!programmer.isPresent(), "device should not answer before bus init")) { return false; }
    if (!expect(i2c.init() && programmer.isPresent(), "device should answer after bus init")) { return false; }

    // Two step read of a 16-bit register, MSB first.
    i2c.setReg(8U, 0xA5C3U);
    std::uint16_t value{};
    if (!expect(programmer.readRegister(8U, value) && (value == 0xA5C3U), "register read")) { return false; }

    // Field read from EEPROM (8) and working register (72).
    const a89301::Field* ratedSpeed{a89301::findField("RATED_SPEED")};
    const a89301::Field* closedLoop{a89301::findField("SPEED_CLOSE_LOOP")};
    if (!expect((ratedSpeed != nullptr) && (closedLoop != nullptr), "fields should exist")) { return false; }
    if (!expect(a89301::findField("NO_SUCH_FIELD") == nullptr, "unknown field")) { return false; }

    i2c.setReg(72U, 0xF923U);
    if (!expect(programmer.readField(*ratedSpeed, false, value) && (value == 0x05C3U), "EEPROM field read")) { return false; }
    if (!expect(programmer.readField(*ratedSpeed, true, value) && (value == 0x0123U), "working field read")) { return false; }
    if (!expect(programmer.readField(*closedLoop, true, value) && (value == 1U), "single bit field read")) { return false; }

    // Field write changes only the field bits of the working register.
    if (!expect(programmer.writeField(*closedLoop, 0U) && (i2c.reg(72U) == 0xF123U),
                "field write should clear only bit 11")) { return false; }
    if (!expect(programmer.writeField(*ratedSpeed, 0x07FFU) && (i2c.reg(72U) == 0xF7FFU),
                "field write should set only bits 10:0")) { return false; }
    if (!expect(!programmer.writeField(*closedLoop, 2U), "too large value should be rejected")) { return false; }
    if (!expect(i2c.reg(8U) == 0xA5C3U, "field write must not touch EEPROM")) { return false; }

    // I2C speed control uses bit 9 and bits 8:0 of register 81 and keeps bits 15:10.
    i2c.setReg(81U, 0xFC00U);
    if (!expect(programmer.setSpeedDemand(200U) && (i2c.reg(81U) == (0xFC00U | 0x0200U | 200U)), "speed demand")) { return false; }
    if (!expect(!programmer.setSpeedDemand(512U), "speed demand above 511 should be rejected")) { return false; }
    if (!expect(programmer.releaseSpeedDemand() && (i2c.reg(81U) == 0xFC00U), "release speed demand")) { return false; }

    // Protected EEPROM addresses are never programmed.
    const std::size_t writesBefore{i2c.writeCount()};
    for (const std::uint8_t address : {0U, 7U, 17U, 19U, 23U, 72U})
    {
        if (!expect(!programmer.programEeprom(address, 0x1234U), "protected EEPROM address")) { return false; }
    }
    if (!expect(i2c.writeCount() == writesBefore, "protected address must not write anything")) { return false; }

    // Programming follows the datasheet sequence and ends with the control register idle.
    // The stub does not emulate EEPROM, so the target value is preset for the read-back check.
    totalDelayMs = 0U;
    i2c.setReg(9U, 0x4321U);
    if (!expect(programmer.programEeprom(9U, 0x4321U), "EEPROM program")) { return false; }
    if (!expect((i2c.reg(162U) == 9U) && (i2c.reg(163U) == 0x4321U) && (i2c.reg(161U) == 0U),
                "EEPROM registers after program")) { return false; }
    if (!expect(totalDelayMs >= 30U, "erase and write pulses should be waited for")) { return false; }
    if (!expect(i2c.writeCount() == writesBefore + 7U, "erase (3) + write (3) + idle (1) writes")) { return false; }

    i2c.setReg(9U, 0x0000U);
    if (!expect(!programmer.programEeprom(9U, 0x4321U), "failed verify should be reported")) { return false; }

    // Words with changed Allegro-only bits are never programmed.
    const std::size_t writesBeforeFixed{i2c.writeCount()};
    if (!expect(!programmer.programEeprom(13U, 0x211EU), "non-default Allegro-only bits should be rejected")) { return false; }
    if (!expect(i2c.writeCount() == writesBeforeFixed, "rejected word must not write anything")) { return false; }

    std::printf("A89301 programmer test succeeded!\n");
    return true;
}
} // namespace test
