/**
 * @file direction.h
 * @brief GPIO direction type.
 */
#pragma once

#include <cstdint>

namespace driver::gpio
{
/**
 * @brief Enumeration of data direction alternatives.
 */
enum class Direction : std::uint8_t
{
    /** @brief Configure pin as a standard digital input (floating). */
    Input,
    
    /** @brief Configure pin as a digital input with an internal pull-up resistor enabled. */
    InputPullup,
    
    /** @brief Configure pin as a standard digital output. */
    Output,
};
} // namespace driver::gpio
