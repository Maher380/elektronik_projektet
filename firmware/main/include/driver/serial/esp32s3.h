/**
 * @file esp32s3.h
 * @brief Serial driver for the ESP32-S3.
 */
#pragma once

#include "driver/serial/interface.h"

#include <cstdint>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace driver::serial
{
struct Config
{
    uart_port_t port;
    int txPin;
    int rxPin;
    int baudRate;
    std::uint32_t rxBufSize;
    bool useUsbJtag{false};
};

class Esp32s3 final : public Interface
{
public:
    explicit Esp32s3(const Config& config) noexcept;
    ~Esp32s3() noexcept override;

    bool connect() noexcept override;
    void disconnect() noexcept override;

    void write(std::uint8_t byte) noexcept override;
    std::uint16_t write(const char* msg) noexcept override;

    std::uint8_t read() noexcept override;
    std::uint16_t read(char* buf, std::uint16_t maxLen) noexcept override;

    bool isDataAvailable() const noexcept override;
    bool isInitialized() const noexcept override;

    Esp32s3(const Esp32s3&) = delete;
    Esp32s3(Esp32s3&&) = delete;
    Esp32s3& operator=(const Esp32s3&) = delete;
    Esp32s3& operator=(Esp32s3&&) = delete;

private:
    static constexpr int QueueDepth{10};
    static constexpr std::uint16_t LineBufSize{64U};

    Config myConfig;
    QueueHandle_t myQueue;
    bool myConnected;
    char myLineBuf[LineBufSize];
    std::uint16_t myLineLen;
};
} // namespace driver::serial
