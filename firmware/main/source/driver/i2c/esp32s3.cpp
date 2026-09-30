/**
 * @file esp32s3.cpp
 * @brief I2C master driver implementation for ESP32-S3.
 */

#include "driver/i2c/esp32s3.h"

#include "system/pin_manager/esp32s3.h"

namespace driver::i2c
{
namespace
{
/** Timeout for one bus transaction in milliseconds. */
constexpr int TransferTimeoutMs{50};

/** Singleton pin manager instance. */
auto& myPinManager = sys::pin_manager::Esp32s3::instance();
} // namespace

Esp32s3::Esp32s3(const Config& config) noexcept
    : myConfig{config}
    , myBus{nullptr}
    , myDevices{}
    , myIsInitialized{false}
{}

Esp32s3::~Esp32s3() noexcept
{
    deinit();
}

bool Esp32s3::init() noexcept
{
    if (myIsInitialized) { return false; }

    if (!myPinManager.reservePin(myConfig.sdaPin)) { return false; }
    if (!myPinManager.reservePin(myConfig.sclPin))
    {
        myPinManager.releasePin(myConfig.sdaPin);
        return false;
    }

    i2c_master_bus_config_t busConfig{};
    busConfig.i2c_port                     = -1; // Select a free I2C port automatically.
    busConfig.sda_io_num                   = static_cast<gpio_num_t>(myConfig.sdaPin);
    busConfig.scl_io_num                   = static_cast<gpio_num_t>(myConfig.sclPin);
    busConfig.clk_source                   = I2C_CLK_SRC_DEFAULT;
    busConfig.glitch_ignore_cnt            = 7U;
    busConfig.flags.enable_internal_pullup = myConfig.enableInternalPullup ? 1U : 0U;

    if (i2c_new_master_bus(&busConfig, &myBus) != ESP_OK)
    {
        myBus = nullptr;
        myPinManager.releasePin(myConfig.sdaPin);
        myPinManager.releasePin(myConfig.sclPin);
        return false;
    }

    myIsInitialized = true;
    return true;
}

bool Esp32s3::deinit() noexcept
{
    if (!myIsInitialized) { return false; }

    for (auto& device : myDevices)
    {
        if (device.handle != nullptr)
        {
            i2c_master_bus_rm_device(device.handle);
            device = Device{};
        }
    }

    const bool deleted{i2c_del_master_bus(myBus) == ESP_OK};
    myBus = nullptr;
    myPinManager.releasePin(myConfig.sdaPin);
    myPinManager.releasePin(myConfig.sclPin);
    myIsInitialized = false;
    return deleted;
}

bool Esp32s3::isInitialized() const noexcept
{
    return myIsInitialized;
}

bool Esp32s3::probe(const std::uint8_t address) noexcept
{
    if (!myIsInitialized) { return false; }
    return i2c_master_probe(myBus, address, TransferTimeoutMs) == ESP_OK;
}

bool Esp32s3::write(const std::uint8_t address, const std::uint8_t* data, const std::size_t length) noexcept
{
    if ((data == nullptr) || (length == 0U)) { return false; }

    const auto handle{device(address)};
    if (handle == nullptr) { return false; }

    return i2c_master_transmit(handle, data, length, TransferTimeoutMs) == ESP_OK;
}

bool Esp32s3::read(const std::uint8_t address, std::uint8_t* data, const std::size_t length) noexcept
{
    if ((data == nullptr) || (length == 0U)) { return false; }

    const auto handle{device(address)};
    if (handle == nullptr) { return false; }

    return i2c_master_receive(handle, data, length, TransferTimeoutMs) == ESP_OK;
}

i2c_master_dev_handle_t Esp32s3::device(const std::uint8_t address) noexcept
{
    if (!myIsInitialized) { return nullptr; }

    for (const auto& device : myDevices)
    {
        if ((device.handle != nullptr) && (device.address == address)) { return device.handle; }
    }

    for (auto& device : myDevices)
    {
        if (device.handle != nullptr) { continue; }

        i2c_device_config_t deviceConfig{};
        deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        deviceConfig.device_address  = address;
        deviceConfig.scl_speed_hz    = myConfig.frequencyHz;

        if (i2c_master_bus_add_device(myBus, &deviceConfig, &device.handle) != ESP_OK)
        {
            device.handle = nullptr;
            return nullptr;
        }

        device.address = address;
        return device.handle;
    }

    return nullptr;
}

} // namespace driver::i2c
