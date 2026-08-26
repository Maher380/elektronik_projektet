/**
 * @file interface.h
 * @brief ADC driver interface
 */
#pragma once 

#include <cstdint>

namespace driver::adc
{
/**
 * @brief ADC driver interface.
 */
class Interface
{
public:    

    /**
     * @brief Destructor.
     */
    virtual ~Interface() noexcept = default;
    
    /**
     * @brief Initialize ADC
     * * @return True if the ADC was initialized successfully, false otherwise.
     */
    virtual bool init() noexcept = 0;

    /**
     * @brief Deinitialize ADC
     * * @return True if the ADC was deinitialized successfully, false otherwise.
     */
    virtual bool deinit() noexcept = 0;

    /**
     * @brief Check if the ADC is initialized.
     * @return True if initialized and ready.
     */
    virtual bool isInitialized() const noexcept = 0;

    /**
     * @brief Read raw digital value from the ADC.
     * For ESP32-S3, this is 0-4095 (12-bit).
     * * @return Raw ADC value.
     */
    virtual std::uint16_t readRaw() const noexcept = 0;

    /**
     * @brief Read the input voltage in Volts.
     * Converts the conversion from raw value to voltage.
     * * @return Input voltage in Volts.
     */
    virtual float readVoltage() const noexcept = 0;
};

} // namespace driver::adc
