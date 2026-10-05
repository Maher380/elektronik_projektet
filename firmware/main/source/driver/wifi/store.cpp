#include "driver/wifi/store.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace driver::wifi
{
namespace
{
/** Layout version, so a future change can be noticed instead of misread. */
constexpr const char* VersionKey{"ver"};

/** Network name. */
constexpr const char* SsidKey{"ssid"};

/** Network password, empty for an open network. */
constexpr const char* PasswordKey{"pass"};

/** WPA2 refuses passphrases shorter than this. */
constexpr std::size_t PasswordMinLength{8U};

/**
 * @brief Length of a string, stopping one past a limit.
 *
 * @param[in] text String, may be nullptr.
 * @param[in] limit Largest length of interest.
 * @return Length, or limit + 1 if the string is longer than limit.
 */
std::size_t boundedLength(const char* text, const std::size_t limit) noexcept
{
    if (text == nullptr) { return 0U; }

    std::size_t length{0U};
    while ((length <= limit) && (text[length] != '\0')) { ++length; }
    return length;
}

} // namespace

// -----------------------------------------------------------------------------
Store::Store(nvs::Interface& nvs) noexcept
    : myHandle{nvs.open(nvs::Namespace::Wifi)}
{}

// -----------------------------------------------------------------------------
bool Store::isOpen() const noexcept
{
    return myHandle != nullptr;
}

// -----------------------------------------------------------------------------
bool Store::isValid(const char* ssid, const char* password) noexcept
{
    if (password == nullptr) { return false; }

    const std::size_t ssidLength{boundedLength(ssid, SsidMaxLength)};
    const std::size_t passwordLength{boundedLength(password, PasswordMaxLength)};

    const bool ssidValid{(ssidLength >= 1U) && (ssidLength <= SsidMaxLength)};
    const bool passwordValid{(passwordLength == 0U)
                             || ((passwordLength >= PasswordMinLength) && (passwordLength <= PasswordMaxLength))};
    return ssidValid && passwordValid;
}

// -----------------------------------------------------------------------------
bool Store::load(char (&ssid)[SsidMaxLength + 1U], char (&password)[PasswordMaxLength + 1U]) const noexcept
{
    if (!isOpen()) { return false; }

    // A missing key is the normal case on a car that was never given a network.
    std::uint8_t version{0U};
    if (!myHandle->getU8(VersionKey, version) || (version != Version)) { return false; }

    // Read into locals, so a partial read cannot leave the caller with half a network.
    char loadedSsid[SsidMaxLength + 1U]{};
    char loadedPassword[PasswordMaxLength + 1U]{};
    if (!myHandle->getString(SsidKey, loadedSsid, sizeof(loadedSsid))) { return false; }
    if (!myHandle->getString(PasswordKey, loadedPassword, sizeof(loadedPassword))) { return false; }
    if (!isValid(loadedSsid, loadedPassword)) { return false; }

    std::memcpy(ssid, loadedSsid, sizeof(loadedSsid));
    std::memcpy(password, loadedPassword, sizeof(loadedPassword));
    return true;
}

// -----------------------------------------------------------------------------
bool Store::save(const char* ssid, const char* password) noexcept
{
    if (!isOpen() || !isValid(ssid, password)) { return false; }

    // Version erased first and written last, so that a write interrupted part way leaves
    // no version and the half-written network is ignored rather than joined.
    myHandle->eraseKey(VersionKey);
    if (!myHandle->setString(SsidKey, ssid)) { return false; }
    if (!myHandle->setString(PasswordKey, password)) { return false; }

    return myHandle->setU8(VersionKey, Version);
}

// -----------------------------------------------------------------------------
bool Store::clear() noexcept
{
    if (!isOpen()) { return false; }

    return myHandle->eraseAll();
}

} // namespace driver::wifi
