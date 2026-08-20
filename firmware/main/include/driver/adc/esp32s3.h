/**
 * @file esp32s3.h
 * @brief ADC driver for ESP32-S3. 
 */
#pragma once

#include "driver/adc/interface.h"

#include "esp_adc/adc_oneshot.h"

namespace driver::adc 
{


class Esp32s3 final : public Interface
{

        
public:    
    /**
     * @brief Constructor.
     * 
     * @param[in] pin The pin to read analog value from (1-10).
     */
    explicit Esp32s3(std::uint8_t pin) noexcept;
    /**
     * @brief Destructor.
     */
    ~Esp32s3() noexcept override = default;
     
    /**
     * @brief Check if the ADC is initialized.
     * 
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override;

     /**
      * @brief Initialize ADC
      *
      * @return True if the ADC was initialized successfully, false otherwise.
      */
    bool init() noexcept override;

     /**
      * @brief Deinitialize ADC
      *
      * @return True if the ADC was deinitialized successfully, false otherwise.
      *
      */
    bool deinit() noexcept override;

    /**
    * @brief Read input from the given pin.
    *
    * @return Input value.
    */
    std::uint16_t readRaw() const noexcept override;


    /**
     * @brief Read the input voltage in Volts.
     * Converts the raw value to voltage.
     * 
     * @return Input voltage in Volts.
     */
    float readVoltage() const noexcept override;

    Esp32s3(const Esp32s3&)            = delete; // No copy constructor.
    Esp32s3(Esp32s3&&)                 = delete; // No move constructor.
    Esp32s3& operator=(const Esp32s3&) = delete; // No copy assignment.
    Esp32s3& operator=(Esp32s3&&)      = delete; // No move assignment.
private:
    /** ADC state. */
    bool myState;
    /** Target ADC pin. */
    std::uint8_t myPin;
    /** ESP-IDF ADC1 channel mapped from the GPIO pin.  */
    adc_channel_t myChannel;
    /** ESP-IDF ADC unit handle. */
    adc_oneshot_unit_handle_t myHandle;
};

} // namespace driver::adc 
