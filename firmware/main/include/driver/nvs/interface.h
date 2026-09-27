/**
 * @file interface.h
 * @brief NVS (non-volatile storage) driver interface.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "driver/nvs/namespaces.h"

namespace driver::nvs
{

/**
 * @brief Read/write access to one NVS namespace, obtained from Interface::open().
 *
 * Every set and erase is saved to flash immediately. Reads return false if the
 * key is missing; the owner then uses its default. Destroying the handle closes
 * the namespace, so it can be opened again.
 */
class Handle
{
public:
    virtual ~Handle() noexcept = default;

    virtual bool setU8(const char* key, std::uint8_t value) noexcept = 0;
    virtual bool getU8(const char* key, std::uint8_t& value) noexcept = 0;

    virtual bool setU32(const char* key, std::uint32_t value) noexcept = 0;
    virtual bool getU32(const char* key, std::uint32_t& value) noexcept = 0;

    virtual bool setString(const char* key, const char* value) noexcept = 0;

    /**
     * @brief Read a string.
     *
     * @param[in] key Key.
     * @param[out] value Buffer for the string including the terminating null.
     * @param[in] size Buffer size in bytes.
     * @return True if found and it fits in the buffer, false otherwise.
     */
    virtual bool getString(const char* key, char* value, std::size_t size) noexcept = 0;

    /** Remove one key. */
    virtual bool eraseKey(const char* key) noexcept = 0;

    /** Remove all keys in this namespace. Other namespaces are untouched. */
    virtual bool eraseAll() noexcept = 0;
};

/**
 * @brief NVS driver: one instance for the whole NVS partition, shared by reference.
 */
class Interface
{
public:
    virtual ~Interface() noexcept = default;

    /**
     * @brief Initialize NVS. Safe to call more than once.
     *
     * If the partition is full or from an incompatible ESP-IDF version, it is
     * erased and initialized again, which loses all stored settings.
     *
     * @return True if NVS is ready to use, false otherwise.
     */
    virtual bool init() noexcept = 0;

    /** @return True if NVS is initialized. */
    virtual bool isInitialized() const noexcept = 0;

    /**
     * @brief Open a namespace. A namespace can only be open once at a time, so two
     *        classes cannot write the same keys by mistake.
     *
     * @param[in] ns Namespace to open.
     * @return Handle, or nullptr if not initialized, already open, or on error.
     */
    virtual std::unique_ptr<Handle> open(Namespace ns) noexcept = 0;
};

} // namespace driver::nvs
