/**
 * @file stub.h
 * @brief I2C driver stub for host tests and simulation.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/i2c/interface.h"

namespace driver::i2c
{

/**
 * @brief Simulated I2C bus with one device that has 256 16-bit registers.
 *
 * The simulated device uses the common "register pointer" protocol:
 * - Writing 1 byte sets the register pointer.
 * - Writing 3 bytes sets the register pointer and writes a 16-bit value, MSB first.
 * - Reading 2 bytes returns the register at the pointer, MSB first.
 */
class Stub final : public Interface
{
public:
    /**
     * @brief Constructor.
     *
     * @param[in] deviceAddress 7-bit address the simulated device answers to.
     */
    explicit Stub(const std::uint8_t deviceAddress) noexcept
        : myDeviceAddress{deviceAddress}
    {}

    /**
     * @brief Destructor.
     */
    ~Stub() noexcept override = default;

    /**
     * @brief Initialize the simulated bus.
     *
     * @return True if the bus was not already initialized, false otherwise.
     */
    bool init() noexcept override
    {
        if (myIsInitialized) { return false; }
        myIsInitialized = true;
        return true;
    }

    /**
     * @brief Deinitialize the simulated bus.
     *
     * @return True if the bus was initialized before the call, false otherwise.
     */
    bool deinit() noexcept override
    {
        if (!myIsInitialized) { return false; }
        myIsInitialized = false;
        return true;
    }

    /**
     * @brief Check if the simulated bus is initialized.
     *
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override
    {
        return myIsInitialized;
    }

    /**
     * @brief Check if the simulated device answers.
     *
     * @param[in] address 7-bit device address.
     * @return True if initialized and the address matches the simulated device.
     */
    bool probe(const std::uint8_t address) noexcept override
    {
        return myIsInitialized && (address == myDeviceAddress);
    }

    /**
     * @brief Write to the simulated device.
     *
     * @param[in] address 7-bit device address.
     * @param[in] data Bytes to write.
     * @param[in] length 1 (set pointer) or 3 (set pointer and write value).
     * @return True if the write was accepted, false otherwise.
     */
    bool write(const std::uint8_t address, const std::uint8_t* data, const std::size_t length) noexcept override
    {
        if (!probe(address) || (data == nullptr) || ((length != 1U) && (length != 3U))) { return false; }

        myPointer = data[0];
        if (length == 3U)
        {
            myRegisters[myPointer] = static_cast<std::uint16_t>((data[1] << 8U) | data[2]);
            ++myWriteCount;
        }
        return true;
    }

    /**
     * @brief Read the register at the pointer from the simulated device.
     *
     * @param[in] address 7-bit device address.
     * @param[out] data Buffer for the 2 received bytes.
     * @param[in] length Must be 2.
     * @return True if the read was accepted, false otherwise.
     */
    bool read(const std::uint8_t address, std::uint8_t* data, const std::size_t length) noexcept override
    {
        if (!probe(address) || (data == nullptr) || (length != 2U)) { return false; }

        data[0] = static_cast<std::uint8_t>(myRegisters[myPointer] >> 8U);
        data[1] = static_cast<std::uint8_t>(myRegisters[myPointer] & 0xFFU);
        return true;
    }

    /**
     * @brief Read a simulated register directly.
     *
     * @param[in] reg Register address.
     * @return Register value.
     */
    std::uint16_t reg(const std::uint8_t reg) const noexcept
    {
        return myRegisters[reg];
    }

    /**
     * @brief Set a simulated register directly.
     *
     * @param[in] reg Register address.
     * @param[in] value Value to store.
     */
    void setReg(const std::uint8_t reg, const std::uint16_t value) noexcept
    {
        myRegisters[reg] = value;
    }

    /**
     * @brief Read the number of 16-bit register writes made over the bus.
     *
     * @return Number of register writes.
     */
    std::size_t writeCount() const noexcept
    {
        return myWriteCount;
    }

    Stub(const Stub&)            = delete;
    Stub& operator=(const Stub&) = delete;
    Stub(Stub&&)                 = delete;
    Stub& operator=(Stub&&)      = delete;

private:
    /** 7-bit address of the simulated device. */
    std::uint8_t myDeviceAddress;

    /** True if the simulated bus is initialized. */
    bool myIsInitialized{false};

    /** Current register pointer. */
    std::uint8_t myPointer{0U};

    /** Number of 16-bit register writes. */
    std::size_t myWriteCount{0U};

    /** Simulated register file. */
    std::array<std::uint16_t, 256U> myRegisters{};
};

} // namespace driver::i2c
