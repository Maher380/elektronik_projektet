/**
 * @file stub.h
 * @brief Serial driver stub for simulation.
 */
#pragma once

#include "driver/serial/interface.h"

#include <cstdint>

namespace driver::serial
{
class Stub final : public Interface
{
public:
    Stub() noexcept;
    ~Stub() noexcept override;

    bool connect() noexcept override;
    void disconnect() noexcept override;

    void write(std::uint8_t byte) noexcept override;
    std::uint16_t write(const char* msg) noexcept override;

    std::uint8_t read() noexcept override;
    std::uint16_t read(char* buf, std::uint16_t maxLen) noexcept override;

    bool isDataAvailable() const noexcept override;
    bool isInitialized() const noexcept override;

    std::uint8_t simulateInput(const std::uint8_t* data, std::uint8_t dataLen) noexcept;

    Stub(const Stub&) = delete;
    Stub(Stub&&) = delete;
    Stub& operator=(const Stub&) = delete;
    Stub& operator=(Stub&&) = delete;

private:
    static constexpr std::uint8_t BufSize{100U};

    std::uint8_t myBuf[BufSize];
    std::uint8_t myBufLen;
    std::uint8_t myBufIndex;
    bool myDataAvailable;
    bool myConnected;
};
} // namespace driver::serial
