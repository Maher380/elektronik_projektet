/**
 * @file pi_link.h
 * @brief The lines the Ford and the Pi's follower exchange over the UART. See ADR 0012.
 *
 * One ASCII line per message: a type, the protocol version and a sequence number, then the
 * fields in a fixed order, then '*' and a CRC-16/CCITT-FALSE of everything before it as
 * four hex digits.
 *
 *     P,1,1234,driving,-12.5,35.0*71D3
 *
 * A line with a bad CRC, the wrong field count, an unknown version or a value out of range
 * is dropped whole, never partly applied. Nothing here touches hardware, so it is tested on
 * the host.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "system/runtime/control.h"

namespace app::pi_link
{

/** The version both ends must send; a line with any other is dropped. */
inline constexpr std::uint32_t ProtocolVersion{1U};
/** Each side sends one line this often, on its own timer, never as a reply. */
inline constexpr std::uint32_t PeriodMs{50U};
/** Longest line either side sends, without the newline. A longer one is dropped. */
inline constexpr std::size_t MaxLineLength{160U};

/**
 * @brief CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no reflection.
 *
 * "123456789" gives 0x29B1.
 */
std::uint16_t crc16(const char* data, std::size_t length) noexcept;

/** One line from the Pi: what the follower is doing, and what it asks for. */
struct PiLine
{
    std::uint32_t sequence{0U};
    runtime::PiState state{runtime::PiState::Waiting};
    /** −90 full left, 0 straight ahead, +90 full right. */
    float steeringCommand{0.0F};
    /** −100 full reverse, 0 no drive, +100 full forward. */
    float speedCommand{0.0F};
};

/**
 * @brief Parse one line from the Pi, without its newline.
 *
 * A trailing carriage return is allowed.
 *
 * @param[in] line The received line.
 * @param[out] out The parsed line; left unchanged unless the result is true.
 * @return True only for a whole, valid line. False means drop it.
 */
bool parsePiLine(const char* line, PiLine& out) noexcept;

/** One line to the Pi: the car's state, as the follower needs it. */
struct CarLine
{
    std::uint32_t sequence{0U};
    const char* controlState{"disarmed"};
    const char* reason{"none"};
    const char* driveStyle{"manual_by_remote"};
    /** Competition mode; always false until it is built. */
    bool competition{false};
    /** none, waiting, started or stopped; none while no start module is fitted. */
    const char* startModule{"none"};
    /** How the car judges the Pi: gone, waiting, driving or lost. */
    const char* piLink{"gone"};
    /** The steering command the car applies, which may differ from what was asked. */
    float appliedSteering{0.0F};
    /** The speed command the car drives to; 0 whenever the motor is not driving. */
    float appliedSpeed{0.0F};
    /** Metres since start-up; NaN, sent as an empty field, without an odometer. */
    float odometerDistanceM{0.0F};
    /** Metres per second, timed between pulses; NaN without an odometer. */
    float odometerSpeedMs{0.0F};
    /** What the odometer speed was worked out from, e.g. per_gap; empty without one. */
    const char* odometerSpeedSource{""};
    /** Sequence of the last valid line received from the Pi. */
    std::uint32_t lastPiSequence{0U};
};

/**
 * @brief Write one line for the Pi, newline included.
 *
 * @param[in] line What to send.
 * @param[out] out Where to write it.
 * @param[in] size Size of out.
 * @return Characters written, excluding the terminating NUL; 0 if it did not fit.
 */
std::size_t formatCarLine(const CarLine& line, char* out, std::size_t size) noexcept;

} // namespace app::pi_link
