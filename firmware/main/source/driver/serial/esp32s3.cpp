#include "driver/serial/esp32s3.h"

#include <cstdint>
#include <cstring>

#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace driver::serial
{
Esp32s3::Esp32s3(const Config& config) noexcept
    : myConfig{config}
    , myQueue{nullptr}
    , myConnected{false}
    , myLineBuf{}
    , myLineLen{0U}
{}

Esp32s3::~Esp32s3() noexcept
{
    if (myConnected)
    {
        disconnect();
    }
}

bool Esp32s3::connect() noexcept
{
    if (myConfig.useUsbJtag)
    {
        usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        const esp_err_t err = usb_serial_jtag_driver_install(&cfg);

        if (err == ESP_OK)
        {
            myQueue = reinterpret_cast<QueueHandle_t>(1);
        }
        else if (err != ESP_ERR_INVALID_STATE)
        {
            return false;
        }

        myConnected = true;
        return true;
    }

    const bool preInstalled = uart_is_driver_installed(myConfig.port);

    if (!preInstalled)
    {
        const uart_config_t uartConfig = {
            .baud_rate = myConfig.baudRate,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk = UART_SCLK_DEFAULT,
            .flags = {},
        };

        if (uart_param_config(myConfig.port, &uartConfig) != ESP_OK)
        {
            return false;
        }

        if (uart_set_pin(myConfig.port,
                         myConfig.txPin,
                         myConfig.rxPin,
                         UART_PIN_NO_CHANGE,
                         UART_PIN_NO_CHANGE) != ESP_OK)
        {
            return false;
        }

        if (uart_driver_install(myConfig.port,
                                static_cast<int>(myConfig.rxBufSize),
                                0,
                                QueueDepth,
                                &myQueue,
                                0) != ESP_OK)
        {
            return false;
        }
    }

    if (uart_enable_pattern_det_baud_intr(myConfig.port, '\n', 1, 9, 0, 0) != ESP_OK)
    {
        if (!preInstalled) { uart_driver_delete(myConfig.port); }
        return false;
    }

    uart_pattern_queue_reset(myConfig.port, QueueDepth);

    myConnected = true;
    return true;
}

void Esp32s3::disconnect() noexcept
{
    if (myConfig.useUsbJtag)
    {
        if (myQueue != nullptr)
        {
            usb_serial_jtag_driver_uninstall();
            myQueue = nullptr;
        }
    }
    else
    {
        uart_disable_pattern_det_intr(myConfig.port);
        if (myQueue != nullptr)
        {
            uart_driver_delete(myConfig.port);
            myQueue = nullptr;
        }
    }

    myConnected = false;
}

void Esp32s3::write(std::uint8_t byte) noexcept
{
    if (!myConnected) { return; }

    if (myConfig.useUsbJtag)
    {
        usb_serial_jtag_write_bytes(&byte, 1U, pdMS_TO_TICKS(10U));
        return;
    }

    uart_write_bytes(myConfig.port, &byte, 1U);
}

std::uint16_t Esp32s3::write(const char* msg) noexcept
{
    if (!myConnected || (msg == nullptr)) { return 0U; }

    const auto len = std::strlen(msg);

    if (myConfig.useUsbJtag)
    {
        const int written = usb_serial_jtag_write_bytes(msg, len, pdMS_TO_TICKS(100U));
        return (written < 0) ? 0U : static_cast<std::uint16_t>(written);
    }

    const int written = uart_write_bytes(myConfig.port, msg, len);
    return (written < 0) ? 0U : static_cast<std::uint16_t>(written);
}

std::uint8_t Esp32s3::read() noexcept
{
    if (!myConnected) { return 0U; }

    std::uint8_t byte{};
    if (myConfig.useUsbJtag)
    {
        usb_serial_jtag_read_bytes(&byte, 1U, pdMS_TO_TICKS(10U));
        return byte;
    }

    uart_read_bytes(myConfig.port, &byte, 1U, pdMS_TO_TICKS(10U));
    return byte;
}

std::uint16_t Esp32s3::read(char* buf, std::uint16_t maxLen) noexcept
{
    if (!myConnected || (buf == nullptr) || (maxLen == 0U)) { return 0U; }

    if (myConfig.useUsbJtag)
    {
        const std::uint16_t limit{static_cast<std::uint16_t>(LineBufSize - 1U)};
        while (myLineLen < limit)
        {
            std::uint8_t ch{};
            if (usb_serial_jtag_read_bytes(&ch, 1U, 0) <= 0) { break; }
            if ((ch == '\n') || (ch == '\r'))
            {
                const std::uint16_t n{(myLineLen < static_cast<std::uint16_t>(maxLen - 1U))
                                           ? myLineLen
                                           : static_cast<std::uint16_t>(maxLen - 1U)};
                std::memcpy(buf, myLineBuf, n);
                buf[n] = '\0';
                myLineLen = 0U;
                return n;
            }
            myLineBuf[myLineLen++] = static_cast<char>(ch);
        }

        buf[0] = '\0';
        return 0U;
    }

    const int patternPos = uart_pattern_pop_pos(myConfig.port);
    if (patternPos < 0) { return 0U; }

    const std::uint16_t limit{static_cast<std::uint16_t>(maxLen - 1U)};
    const std::uint16_t toRead{(static_cast<std::uint16_t>(patternPos) < limit)
                                   ? static_cast<std::uint16_t>(patternPos)
                                   : limit};

    const int result = uart_read_bytes(myConfig.port,
                                       reinterpret_cast<std::uint8_t*>(buf),
                                       toRead,
                                       pdMS_TO_TICKS(100U));

    std::uint8_t newline{};
    uart_read_bytes(myConfig.port, &newline, 1U, pdMS_TO_TICKS(10U));

    const std::uint16_t bytesRead{result < 0 ? std::uint16_t{0U} : static_cast<std::uint16_t>(result)};
    buf[bytesRead] = '\0';
    return bytesRead;
}

bool Esp32s3::isDataAvailable() const noexcept
{
    if (!myConnected) { return false; }
    if (myConfig.useUsbJtag) { return myLineLen > 0U; }
    return uart_pattern_get_pos(myConfig.port) >= 0;
}

bool Esp32s3::isInitialized() const noexcept
{
    return myConnected;
}
} // namespace driver::serial
