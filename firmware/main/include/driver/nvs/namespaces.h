/**
 * @file namespaces.h
 * @brief Registry of all NVS namespaces. Each namespace has exactly one owning class.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace driver::nvs
{

/** Registered NVS namespaces. Add new ones before Count, with a name in NamespaceNames. */
enum class Namespace : std::uint8_t
{
    Test, ///< Used by host tests only.
    Odometer, ///< One wheel's measured magnet gap table. Owner: driver::odometer::Store.
    Count ///< Number of namespaces, not a namespace.
};

/**
 * Namespace names in flash (max 15 characters), in the same order as Namespace.
 * Never use "phy" or "nvs.net80211", they belong to ESP-IDF.
 */
inline constexpr const char* NamespaceNames[]{
    "test",
    "odo",
};

static_assert(sizeof(NamespaceNames) / sizeof(NamespaceNames[0]) == static_cast<std::size_t>(Namespace::Count),
              "Every Namespace needs a name in NamespaceNames");

} // namespace driver::nvs
