/**
 * @file interface.h
 * @brief Interface for the serial driver.
 */
#pragma once

#include <cstdint>

namespace driver::serial
{
class Interface
{
public:
    virtual ~Interface() noexcept = default;

    virtual bool connect() noexcept = 0;
    virtual void disconnect() noexcept = 0;

    virtual void write(std::uint8_t byte) noexcept = 0;
    virtual std::uint16_t write(const char* msg) noexcept = 0;

    virtual std::uint8_t read() noexcept = 0;
    virtual std::uint16_t read(char* buf, std::uint16_t maxLen) noexcept = 0;

    virtual bool isDataAvailable() const noexcept = 0;
    virtual bool isInitialized() const noexcept = 0;
};
} // namespace driver::serial
