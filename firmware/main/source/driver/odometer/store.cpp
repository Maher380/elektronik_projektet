#include "driver/odometer/store.h"

#include <cstddef>
#include <cstdint>

namespace driver::odometer
{
namespace
{
/** Layout version, so a future change can be noticed instead of misread. */
constexpr const char* VersionKey{"ver"};

/** Magnets the table describes. A table is useless against a different count. */
constexpr char MagnetsKey[]{"n"};

/** Largest disagreement between wheel speeds when the table was measured. */
constexpr char SpreadKey[]{"spread"};

/** Longest gap key is "g15" plus its terminator. */
constexpr std::size_t GapKeySize{4U};

/**
 * @brief Build the key for one gap, "g0" through "g15".
 *
 * @param[in] index Gap index, below MaxPulsesPerRevolution.
 * @param[out] out Buffer of at least GapKeySize bytes.
 */
void gapKey(const std::uint8_t index, char* out) noexcept
{
    out[0] = 'g';
    if (index < 10U)
    {
        out[1] = static_cast<char>('0' + index);
        out[2] = '\0';
    }
    else
    {
        out[1] = '1';
        out[2] = static_cast<char>('0' + (index - 10U));
        out[3] = '\0';
    }
}

} // namespace

// -----------------------------------------------------------------------------
Store::Store(nvs::Interface& nvs) noexcept
    : myHandle{nvs.open(nvs::Namespace::Odometer)}
{}

// -----------------------------------------------------------------------------
bool Store::isOpen() const noexcept
{
    return myHandle != nullptr;
}

// -----------------------------------------------------------------------------
bool Store::load(const std::uint8_t magnets, GapTable& table, float& spread) const noexcept
{
    spread = 0.0F;

    if (!isOpen()) { return false; }
    if ((magnets == 0U) || (magnets > MaxPulsesPerRevolution)) { return false; }

    // A missing key is the normal case on a car that has never been calibrated.
    std::uint8_t version{0U};
    if (!myHandle->getU8(VersionKey, version) || (version != Version)) { return false; }

    std::uint8_t storedMagnets{0U};
    if (!myHandle->getU8(MagnetsKey, storedMagnets) || (storedMagnets != magnets)) { return false; }

    // Build into a local, so a partial read cannot leave the caller with half a table.
    GapTable loaded{};
    loaded.count = storedMagnets;
    for (std::uint8_t index{0U}; index < storedMagnets; ++index)
    {
        char key[GapKeySize]{};
        gapKey(index, key);

        if (!myHandle->getFloat(key, loaded.fraction[index])) { return false; }
    }

    if (!isPlausible(loaded)) { return false; }

    // Evidence of how the table was measured; its absence does not invalidate it.
    float storedSpread{0.0F};
    if (myHandle->getFloat(SpreadKey, storedSpread)) { spread = storedSpread; }

    table = loaded;
    return true;
}

// -----------------------------------------------------------------------------
bool Store::save(const GapTable& table, const float spread) noexcept
{
    if (!isOpen() || !isPlausible(table)) { return false; }

    // Gaps first, then the version last, so that a write interrupted part way leaves a
    // version that does not match and the table is ignored rather than half believed.
    for (std::uint8_t index{0U}; index < table.count; ++index)
    {
        char key[GapKeySize]{};
        gapKey(index, key);

        if (!myHandle->setFloat(key, table.fraction[index])) { return false; }
    }

    if (!myHandle->setFloat(SpreadKey, spread)) { return false; }
    if (!myHandle->setU8(MagnetsKey, table.count)) { return false; }

    return myHandle->setU8(VersionKey, Version);
}

// -----------------------------------------------------------------------------
bool Store::clear() noexcept
{
    if (!isOpen()) { return false; }

    return myHandle->eraseAll();
}

} // namespace driver::odometer
