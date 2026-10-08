#include "system/communication/pi_link.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace app::pi_link
{
namespace
{
/** Fields in a P line: type, version, sequence, state, steering, speed. */
constexpr std::size_t PiFieldCount{6U};

bool parseHex4(const char* text, std::uint16_t& value) noexcept
{
    std::uint16_t result{0U};
    for (std::size_t index{0U}; index < 4U; ++index)
    {
        const char ch{text[index]};
        std::uint16_t digit{0U};
        if ((ch >= '0') && (ch <= '9')) { digit = static_cast<std::uint16_t>(ch - '0'); }
        else if ((ch >= 'A') && (ch <= 'F')) { digit = static_cast<std::uint16_t>(ch - 'A' + 10); }
        else if ((ch >= 'a') && (ch <= 'f')) { digit = static_cast<std::uint16_t>(ch - 'a' + 10); }
        else { return false; }
        result = static_cast<std::uint16_t>((result << 4U) | digit);
    }
    value = result;
    return true;
}

/** A whole unsigned decimal number, nothing else; strtoul alone accepts "-1" and " 1". */
bool parseUnsigned(const char* text, std::uint32_t& value) noexcept
{
    if ((*text < '0') || (*text > '9')) { return false; }
    char* end{nullptr};
    const unsigned long result{std::strtoul(text, &end, 10)};
    if ((*end != '\0') || (result > 0xFFFFFFFFUL)) { return false; }
    value = static_cast<std::uint32_t>(result);
    return true;
}

/** A whole, finite number within [minimum, maximum], nothing else. */
bool parseFloat(const char* text, const float minimum, const float maximum, float& value) noexcept
{
    if (*text == '\0') { return false; }
    char* end{nullptr};
    const float result{std::strtof(text, &end)};
    if ((*end != '\0') || !std::isfinite(result) || (result < minimum) || (result > maximum))
    {
        return false;
    }
    value = result;
    return true;
}

bool parseState(const char* text, runtime::PiState& state) noexcept
{
    if (std::strcmp(text, "waiting") == 0) { state = runtime::PiState::Waiting; return true; }
    if (std::strcmp(text, "driving") == 0) { state = runtime::PiState::Driving; return true; }
    if (std::strcmp(text, "lost") == 0) { state = runtime::PiState::Lost; return true; }
    return false;
}

/** Append text, keeping count; false once it no longer fits. */
bool append(char* out, const std::size_t size, std::size_t& used, const char* format, ...) noexcept
    __attribute__((format(printf, 4, 5)));

bool append(char* out, const std::size_t size, std::size_t& used, const char* format, ...) noexcept
{
    if (used >= size) { return false; }
    va_list args;
    va_start(args, format);
    const int written{std::vsnprintf(out + used, size - used, format, args)};
    va_end(args);
    if ((written < 0) || (static_cast<std::size_t>(written) >= size - used)) { return false; }
    used += static_cast<std::size_t>(written);
    return true;
}

/** A measured value, or an empty field when there is none. */
bool appendMeasured(char* out, const std::size_t size, std::size_t& used, const float value,
                    const char* format) noexcept
{
    if (!std::isfinite(value)) { return append(out, size, used, ","); }
    return append(out, size, used, format, static_cast<double>(value));
}
} // namespace

std::uint16_t crc16(const char* data, const std::size_t length) noexcept
{
    std::uint16_t crc{0xFFFFU};
    for (std::size_t index{0U}; index < length; ++index)
    {
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(static_cast<unsigned char>(data[index])) << 8U));
        for (int bit{0}; bit < 8; ++bit)
        {
            crc = (crc & 0x8000U) != 0U ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                                        : static_cast<std::uint16_t>(crc << 1U);
        }
    }
    return crc;
}

bool parsePiLine(const char* line, PiLine& out) noexcept
{
    if (line == nullptr) { return false; }
    std::size_t length{std::strlen(line)};
    if ((length > 0U) && (line[length - 1U] == '\r')) { --length; }
    // The shortest possible line is longer than this; a longer one was never sent whole.
    if ((length < 8U) || (length > MaxLineLength)) { return false; }

    // The CRC is the last five characters: '*' and four hex digits.
    const std::size_t star{length - 5U};
    std::uint16_t sent{0U};
    if ((line[star] != '*') || !parseHex4(line + star + 1U, sent)) { return false; }
    if (crc16(line, star) != sent) { return false; }

    char body[MaxLineLength + 1U]{};
    std::memcpy(body, line, star);
    body[star] = '\0';

    const char* fields[PiFieldCount]{};
    std::size_t count{0U};
    char* cursor{body};
    while (true)
    {
        if (count == PiFieldCount) { return false; }
        fields[count++] = cursor;
        char* comma{std::strchr(cursor, ',')};
        if (comma == nullptr) { break; }
        *comma = '\0';
        cursor = comma + 1;
    }
    if (count != PiFieldCount) { return false; }

    PiLine parsed{};
    std::uint32_t version{0U};
    if ((std::strcmp(fields[0], "P") != 0)
        || !parseUnsigned(fields[1], version) || (version != ProtocolVersion)
        || !parseUnsigned(fields[2], parsed.sequence)
        || !parseState(fields[3], parsed.state)
        || !parseFloat(fields[4], -90.0F, 90.0F, parsed.steeringCommand)
        || !parseFloat(fields[5], -100.0F, 100.0F, parsed.speedCommand))
    {
        return false;
    }
    out = parsed;
    return true;
}

std::size_t formatCarLine(const CarLine& line, char* out, const std::size_t size) noexcept
{
    if ((out == nullptr) || (size == 0U)) { return 0U; }
    std::size_t used{0U};
    const bool ok = append(out, size, used, "C,%lu,%lu,%s,%s,%s,%d,%s,%s,%.1f,%.1f",
                           static_cast<unsigned long>(ProtocolVersion),
                           static_cast<unsigned long>(line.sequence), line.controlState,
                           line.reason, line.driveStyle, line.competition ? 1 : 0,
                           line.startModule, line.piLink,
                           static_cast<double>(line.appliedSteering),
                           static_cast<double>(line.appliedSpeed))
        && appendMeasured(out, size, used, line.odometerDistanceM, ",%.3f")
        && appendMeasured(out, size, used, line.odometerSpeedMs, ",%.3f")
        && append(out, size, used, ",%s,%lu", line.odometerSpeedSource,
                  static_cast<unsigned long>(line.lastPiSequence))
        && appendMeasured(out, size, used, line.distanceForwardCm, ",%.1f")
        && appendMeasured(out, size, used, line.distanceLeftCm, ",%.1f")
        && appendMeasured(out, size, used, line.distanceRightCm, ",%.1f");
    const std::uint16_t crc{ok ? crc16(out, used) : std::uint16_t{0U}};
    // The newline does not count towards MaxLineLength; everything before it does.
    if (!ok || !append(out, size, used, "*%04X\n", static_cast<unsigned>(crc))
        || ((used - 1U) > MaxLineLength))
    {
        out[0] = '\0';
        return 0U;
    }
    return used;
}

} // namespace app::pi_link
