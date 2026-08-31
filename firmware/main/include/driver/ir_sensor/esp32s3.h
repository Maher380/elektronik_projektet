/**
 * @file IR sensor ESP32-S3 driver interface.
 */

#pragma once 

#include "driver/ir_sensor/interface.h"
#include "driver/adc/interface.h"

namespace driver::ir_sensor
{

class Esp32s3 final : public Interface
{

public:
    
    /**
     * @brief Constructor.
     * 
     * @param[in] adc Reference to an ADC-driver interface.
     */
    Esp32s3(driver::adc::Interface& adc) noexcept ;

    /**
     * @brief Destructor.
     */
    ~Esp32s3() noexcept override = default;

    /**
     * @brief Read the current distance from the sensor.
     * 
     * @return Distance in cm
     */
    float readDistance() noexcept override;

    /**
     * @brief Check if sensor is running.
     * 
     * @return True if enabled, false otherwise.
     */
    bool isInitialized() const noexcept override;

    private: 
    
    /** 
     * @brief the adc used by the IR sensor 
     * 
     */
    driver::adc::Interface &myAdc;
};
} // namespace driver::ir_sensor