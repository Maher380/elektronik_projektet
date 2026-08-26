#include "driver/serial/stub.h"

#include <cstdio>
#include <cstdint>

namespace driver::serial
{
Stub::Stub() noexcept
    : myBuf{}
    , myBufLen{}
    , myBufIndex{}
    , myDataAvailable{false}
    , myConnected{false}
{}

Stub::~Stub() noexcept = default;

bool Stub::connect() noexcept
{
    myConnected = true;
    return true;
}

void Stub::disconnect() noexcept
{
    myConnected = false;
}

void Stub::write(std::uint8_t byte) noexcept
{
    if (!myConnected) { return; }
    std::printf("%u", byte);
}

std::uint16_t Stub::write(const char* msg) noexcept
{
    if (!myConnected || (msg == nullptr)) { return 0U; }

    std::uint16_t i{};
    for (i = 0U; msg[i] != '\0'; ++i)
    {
        std::printf("%c", msg[i]);
    }

    return i;
}

std::uint8_t Stub::read() noexcept
{
    if (!myConnected || !myDataAvailable) { return 0U; }

    const std::uint8_t byte{myBuf[myBufIndex]};
    if (++myBufIndex >= myBufLen)
    {
        myDataAvailable = false;
        myBufIndex = 0U;
    }

    return byte;
}

std::uint16_t Stub::read(char* buf, std::uint16_t maxLen) noexcept
{
    if (!myConnected || (buf == nullptr) || (maxLen == 0U) || !myDataAvailable)
    {
        return 0U;
    }

    const std::uint16_t limit{static_cast<std::uint16_t>(maxLen - 1U)};
    std::uint16_t bytesRead{};

    while ((bytesRead < limit) && myDataAvailable)
    {
        const std::uint8_t byte{myBuf[myBufIndex]};

        if (++myBufIndex >= myBufLen)
        {
            myDataAvailable = false;
            myBufIndex = 0U;
        }

        if (byte == '\n') { break; }
        buf[bytesRead++] = static_cast<char>(byte);
    }

    buf[bytesRead] = '\0';
    return bytesRead;
}

bool Stub::isDataAvailable() const noexcept
{
    return myDataAvailable;
}

bool Stub::isInitialized() const noexcept
{
    return myConnected;
}

std::uint8_t Stub::simulateInput(const std::uint8_t* data, std::uint8_t dataLen) noexcept
{
    if (!myConnected || (data == nullptr) || (dataLen == 0U)) { return 0U; }

    const std::uint8_t bytesToCopy{BufSize < dataLen ? BufSize : dataLen};
    for (std::uint8_t i{}; i < bytesToCopy; ++i)
    {
        myBuf[i] = data[i];
    }

    myBufLen = bytesToCopy;
    myDataAvailable = true;
    return bytesToCopy;
}
} // namespace driver::serial
