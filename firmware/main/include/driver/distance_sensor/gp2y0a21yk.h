/**
 * @file GP2Y0A21YK IR sensor driver.
 */

#pragma once 

#include "driver/distance_sensor/interface.h"
#include "driver/adc/interface.h"

namespace driver::distance_sensor
{

class GP2Y0A21YK final : public Interface
{

public:
    
    /**
     * @brief Constructor.
     * 
     * @param[in] adc Reference to an ADC-driver interface.
     */
    GP2Y0A21YK(driver::adc::Interface& adc) noexcept ;

    /**
     * @brief Destructor.
     */
    ~GP2Y0A21YK() noexcept override = default;

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
} // namespace driver::distance_sensor