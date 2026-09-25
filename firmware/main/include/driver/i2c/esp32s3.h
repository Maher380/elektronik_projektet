/**
 * @file esp32s3.h
 * @brief I2C master driver for ESP32-S3.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c/interface.h"
#include "driver/i2c_master.h"

namespace driver::i2c
{

/**
 * @brief Default I2C clock frequency in Hz.
 */
inline constexpr std::uint32_t DefaultFrequencyHz{100'000U};

/**
 * @brief Configuration for an I2C master bus.
 */
struct Config
{
    /** GPIO pin used as SDA. */
    std::uint8_t sdaPin{0U};

    /** GPIO pin used as SCL. */
    std::uint8_t sclPin{0U};

    /** SCL clock frequency in Hz. */
    std::uint32_t frequencyHz{DefaultFrequencyHz};

    /** True to enable the weak internal pull-ups. External pull-ups are still recommended. */
    bool enableInternalPullup{true};
};

/**
 * @brief I2C master driver implementation for ESP32-S3.
 *
 * Device handles are created on first use and cached, so several devices can share the bus.
 * This class cannot be copied or moved.
 */
class Esp32s3 final : public Interface
{
public:
    /**
     * @brief Constructor.
     *
     * @param[in] config I2C bus configuration.
     */
    explicit Esp32s3(const Config& config) noexcept;

    /**
     * @brief Destructor. Releases the bus and its pins.
     */
    ~Esp32s3() noexcept override;

    /**
     * @brief Reserve the pins and create the I2C master bus.
     *
     * @return True if the bus was initialized successfully, false otherwise.
     */
    bool init() noexcept override;

    /**
     * @brief Delete the I2C master bus and release the pins.
     *
     * @return True if the bus was deinitialized successfully, false otherwise.
     */
    bool deinit() noexcept override;

    /**
     * @brief Check if the I2C bus is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override;

    /**
     * @brief Check if a device acknowledges its address.
     *
     * @param[in] address 7-bit device address.
     * @return True if the device answered with ACK, false otherwise.
     */
    bool probe(std::uint8_t address) noexcept override;

    /**
     * @brief Write bytes to a device in one transaction.
     *
     * @param[in] address 7-bit device address.
     * @param[in] data Bytes to write.
     * @param[in] length Number of bytes to write.
     * @return True if all bytes were acknowledged, false otherwise.
     */
    bool write(std::uint8_t address, const std::uint8_t* data, std::size_t length) noexcept override;

    /**
     * @brief Read bytes from a device in one transaction.
     *
     * @param[in] address 7-bit device address.
     * @param[out] data Buffer for the received bytes.
     * @param[in] length Number of bytes to read.
     * @return True if the bytes were read, false otherwise.
     */
    bool read(std::uint8_t address, std::uint8_t* data, std::size_t length) noexcept override;

    Esp32s3(const Esp32s3&)            = delete;
    Esp32s3& operator=(const Esp32s3&) = delete;
    Esp32s3(Esp32s3&&)                 = delete;
    Esp32s3& operator=(Esp32s3&&)      = delete;

private:
    /** Maximum number of devices that can be cached on the bus. */
    static constexpr std::size_t MaxDevices{4U};

    /** Cached device handle for one address. */
    struct Device
    {
        /** 7-bit device address. */
        std::uint8_t address{0U};

        /** ESP-IDF device handle, nullptr if the slot is free. */
        i2c_master_dev_handle_t handle{nullptr};
    };

    /**
     * @brief Get the cached device handle for an address, creating it if needed.
     *
     * @param[in] address 7-bit device address.
     * @return Device handle, or nullptr if the bus is not initialized or all slots are used.
     */
    i2c_master_dev_handle_t device(std::uint8_t address) noexcept;

    /** Bus configuration. */
    const Config myConfig;

    /** ESP-IDF bus handle. */
    i2c_master_bus_handle_t myBus;

    /** Cached device handles. */
    Device myDevices[MaxDevices];

    /** True after successful initialization. */
    bool myIsInitialized;
};

} // namespace driver::i2c
