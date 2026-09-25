/**
 * @file interface.h
 * @brief Abstract I2C master driver interface.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace driver::i2c
{

/**
 * @brief Abstract interface for I2C master drivers.
 *
 * Each call is one complete bus transaction (START ... STOP) addressed to a 7-bit device address.
 */
class Interface
{
public:
    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;

    /**
     * @brief Initialize the I2C bus.
     *
     * @return True if the bus was initialized successfully, false otherwise.
     */
    virtual bool init() noexcept = 0;

    /**
     * @brief Deinitialize the I2C bus.
     *
     * @return True if the bus was deinitialized successfully, false otherwise.
     */
    virtual bool deinit() noexcept = 0;

    /**
     * @brief Check if the I2C bus is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    virtual bool isInitialized() const noexcept = 0;

    /**
     * @brief Check if a device acknowledges its address.
     *
     * @param[in] address 7-bit device address.
     * @return True if the device answered with ACK, false otherwise.
     */
    virtual bool probe(std::uint8_t address) noexcept = 0;

    /**
     * @brief Write bytes to a device in one transaction.
     *
     * @param[in] address 7-bit device address.
     * @param[in] data Bytes to write.
     * @param[in] length Number of bytes to write.
     * @return True if all bytes were acknowledged, false otherwise.
     */
    virtual bool write(std::uint8_t address, const std::uint8_t* data, std::size_t length) noexcept = 0;

    /**
     * @brief Read bytes from a device in one transaction.
     *
     * @param[in] address 7-bit device address.
     * @param[out] data Buffer for the received bytes.
     * @param[in] length Number of bytes to read.
     * @return True if the bytes were read, false otherwise.
     */
    virtual bool read(std::uint8_t address, std::uint8_t* data, std::size_t length) noexcept = 0;
};

} // namespace driver::i2c
