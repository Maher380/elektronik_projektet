/**
 * @file store.h
 * @brief Persistence for the Wi-Fi network the car joins.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "driver/nvs/interface.h"

namespace driver::wifi
{

/** Longest SSID Wi-Fi allows, in bytes. */
inline constexpr std::size_t SsidMaxLength{32U};

/** Longest WPA2 passphrase, in bytes. */
inline constexpr std::size_t PasswordMaxLength{63U};

/**
 * @brief Owns the `wifi` NVS namespace, which holds one network's SSID and password.
 *
 * The Wi-Fi driver deliberately does not read flash: it is handed the credentials and
 * knows nothing about where they came from. This class is the only thing that touches the
 * namespace, as rule 1 of nvs_usage.md requires, and the key names are private to its
 * implementation.
 *
 * Nothing stored is normal, not a fault. The caller then uses the network compiled into
 * the firmware.
 *
 * @attention NVS is not encrypted: anyone with the car and a USB cable can read the
 *            password back, exactly as they could from the firmware image before.
 */
class Store final
{
public:
    /** Layout version written alongside the credentials; a mismatch makes a load fail. */
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
     * @brief Read the stored network.
     *
     * @param[out] ssid Buffer of at least SsidMaxLength + 1 bytes.
     * @param[out] password Buffer of at least PasswordMaxLength + 1 bytes. Empty for an
     *                      open network.
     * @return False if nothing is stored or the layout version differs. The buffers are
     *         untouched unless the whole read succeeded.
     */
    bool load(char (&ssid)[SsidMaxLength + 1U], char (&password)[PasswordMaxLength + 1U]) const noexcept;

    /**
     * @brief Write a network, replacing whatever was there.
     *
     * @param[in] ssid SSID, 1 to SsidMaxLength bytes.
     * @param[in] password Password, empty for an open network or 8 to PasswordMaxLength bytes.
     * @return True if every key was written; false and nothing written if a value is invalid.
     */
    bool save(const char* ssid, const char* password) noexcept;

    /**
     * @brief Forget the stored network, so the car falls back to the compiled-in one.
     *
     * @return True if the namespace was cleared.
     */
    bool clear() noexcept;

    /**
     * @brief Check an SSID and password are something Wi-Fi can actually use.
     *
     * @param[in] ssid SSID.
     * @param[in] password Password.
     * @return True if the SSID is 1 to 32 bytes and the password is empty or 8 to 63 bytes.
     */
    static bool isValid(const char* ssid, const char* password) noexcept;

    Store(const Store&)            = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&)                 = delete;
    Store& operator=(Store&&)      = delete;

private:
    /** Open namespace, or nullptr if the open failed. */
    std::unique_ptr<nvs::Handle> myHandle;
};

} // namespace driver::wifi
