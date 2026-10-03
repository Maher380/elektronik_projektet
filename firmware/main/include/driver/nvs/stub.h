/**
 * @file stub.h
 * @brief NVS driver stub that keeps values in RAM, for host tests.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "driver/nvs/interface.h"

namespace driver::nvs
{

class Stub final : public Interface
{
public:
    Stub() noexcept = default;
    ~Stub() noexcept override = default;

    bool init() noexcept override
    {
        myInitialized = true;
        return true;
    }

    bool isInitialized() const noexcept override { return myInitialized; }

    std::unique_ptr<Handle> open(const Namespace ns) noexcept override
    {
        const auto index{static_cast<std::size_t>(ns)};
        if (!myInitialized || (index >= Count) || myOpen[index]) { return nullptr; }

        myOpen[index] = true;
        return std::make_unique<StubHandle>(myStorage[index], myOpen[index]);
    }

    Stub(const Stub&)            = delete;
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(Stub&&)      = delete;

private:
    static constexpr std::size_t Count{static_cast<std::size_t>(Namespace::Count)};

    enum class Type : std::uint8_t { U8, U32, Float, String };

    struct Entry
    {
        Type type{Type::U8};
        std::uint32_t number{0U};
        std::string text{};
        float real{0.0F};
    };

    using Storage = std::map<std::string, Entry>;

    class StubHandle final : public Handle
    {
    public:
        StubHandle(Storage& storage, bool& open) noexcept
            : myStorage{storage}
            , myOpen{open}
        {}

        ~StubHandle() noexcept override { myOpen = false; }

        bool setU8(const char* key, const std::uint8_t value) noexcept override
        {
            myStorage[key] = Entry{Type::U8, value, {}};
            return true;
        }

        bool getU8(const char* key, std::uint8_t& value) noexcept override
        {
            const Entry* entry{find(key, Type::U8)};
            if (entry == nullptr) { return false; }
            value = static_cast<std::uint8_t>(entry->number);
            return true;
        }

        bool setU32(const char* key, const std::uint32_t value) noexcept override
        {
            myStorage[key] = Entry{Type::U32, value, {}};
            return true;
        }

        bool getU32(const char* key, std::uint32_t& value) noexcept override
        {
            const Entry* entry{find(key, Type::U32)};
            if (entry == nullptr) { return false; }
            value = entry->number;
            return true;
        }

        bool setFloat(const char* key, const float value) noexcept override
        {
            myStorage[key] = Entry{Type::Float, 0U, {}, value};
            return true;
        }

        bool getFloat(const char* key, float& value) noexcept override
        {
            const Entry* entry{find(key, Type::Float)};
            if (entry == nullptr) { return false; }
            value = entry->real;
            return true;
        }

        bool setString(const char* key, const char* value) noexcept override
        {
            if (value == nullptr) { return false; }
            myStorage[key] = Entry{Type::String, 0U, value};
            return true;
        }

        bool getString(const char* key, char* value, const std::size_t size) noexcept override
        {
            const Entry* entry{find(key, Type::String)};
            if ((entry == nullptr) || (value == nullptr) || (size <= entry->text.size())) { return false; }
            std::memcpy(value, entry->text.c_str(), entry->text.size() + 1U);
            return true;
        }

        bool eraseKey(const char* key) noexcept override { return myStorage.erase(key) > 0U; }

        bool eraseAll() noexcept override
        {
            myStorage.clear();
            return true;
        }

    private:
        const Entry* find(const char* key, const Type type) const noexcept
        {
            const auto it{myStorage.find(key)};
            return ((it != myStorage.end()) && (it->second.type == type)) ? &it->second : nullptr;
        }

        Storage& myStorage;
        bool& myOpen;
    };

    Storage myStorage[Count]{};
    bool myOpen[Count]{};
    bool myInitialized{false};
};

} // namespace driver::nvs
