/**
 * @file store.h
 * @brief Persistence for one wheel's measured magnet gap table.
 */

#pragma once

#include <cstdint>
#include <memory>

#include "driver/nvs/interface.h"
#include "driver/odometer/gaps.h"

namespace driver::odometer
{

/**
 * @brief Owns the `odo` NVS namespace, which holds one wheel's measured gap table.
 *
 * The odometer driver deliberately does not read flash: it is handed a table and knows
 * nothing about where it came from. This class is the only thing that touches the
 * namespace, as rule 1 of nvs_usage.md requires, and the key names are private to its
 * implementation.
 *
 * A missing table is normal, not a fault. The caller then uses the wheel's design values,
 * which are already far better than treating the gaps as equal.
 */
class Store final
{
public:
    /** Layout version written alongside the table; a mismatch makes a load fail. */
    static constexpr std::uint8_t Version{1U};

    /**
     * @brief Constructor. Opens the namespace, which stays open for the object's life.
     *
     * @param[in] nvs Initialized NVS driver.
     */
    explicit Store(nvs::Interface& nvs) noexcept;

    /**
     * @brief Destructor. Closes the namespace so it can be opened again.
     */
    ~Store() noexcept = default;

    /**
     * @brief Check the namespace was opened.
     *
     * @return True if the store can be used. False if NVS was not initialized, or the
     *         namespace was already open elsewhere, which would mean two owners.
     */
    bool isOpen() const noexcept;

    /**
     * @brief Read the stored table.
     *
     * @param[out] table The stored table, untouched unless the whole read succeeded.
     * @param[out] spread Largest disagreement between wheel speeds when it was measured,
     *                    as evidence of how believable it is. 0 if it was not recorded.
     * @return False if nothing is stored, the layout version differs, the magnet count
     *         does not match what was asked for, or the fractions are not plausible.
     */
    bool load(std::uint8_t magnets, GapTable& table, float& spread) const noexcept;

    /**
     * @brief Write a table, replacing whatever was there.
     *
     * @param[in] table Table to store; refused unless plausible.
     * @param[in] spread Largest disagreement between wheel speeds when it was measured.
     * @return True if every key was written.
     */
    bool save(const GapTable& table, float spread) noexcept;

    /**
     * @brief Forget the stored table, so the wheel falls back to its design values.
     *
     * @return True if the namespace was cleared.
     */
    bool clear() noexcept;

    // Delete default construction, copy and move: this object owns a namespace.
    Store()                        = delete;
    Store(const Store&)            = delete;
    Store(Store&&)                 = delete;
    Store& operator=(const Store&) = delete;
    Store& operator=(Store&&)      = delete;

private:
    /** Handle to the `odo` namespace, or nullptr if it could not be opened. */
    std::unique_ptr<nvs::Handle> myHandle;
};

} // namespace driver::odometer
