/**
 * @file edge.h
 * @brief GPIO interrupt edge type.
 */
#pragma once

#include <cstdint>

namespace driver::gpio
{
/**
 * @brief Enumeration of signal edges that can trigger a GPIO interrupt.
 */
enum class Edge : std::uint8_t
{
    /** @brief Trigger on a low-to-high transition. */
    Rising,

    /** @brief Trigger on a high-to-low transition. */
    Falling,

    /** @brief Trigger on any transition. */
    Both,
};
} // namespace driver::gpio
