/**
 * @file esp32s3.h
 * @brief Real hardware interface for the ESP32-S3 timer driver.
 */

#pragma once
#include "driver/timer/interface.h"
#include "driver/gptimer.h"
#include "esp_attr.h"

namespace driver::timer {

/**
 * @brief Real hardware interface for the ESP32-S3 timer driver.
 * This class configures and control a hardware Genreal Purpose Timer (GPTimer)
 * This class cannot be copied or moved.
 */
class Esp32s3 final : public Interface {
public:

     // Delete no copy/move contrustors and operators.
    Esp32s3(const Esp32s3&)            = delete;
    Esp32s3& operator=(const Esp32s3&) = delete;
    Esp32s3(Esp32s3&&)                 = delete;
    Esp32s3& operator=(Esp32s3&&)      = delete;

    /**
     * @brief Constructor that initializes the GPTimer hardware.
     */
    Esp32s3() noexcept;

    /**
     * @brief Destructor that ensures hardware resources are released.
     */
    virtual ~Esp32s3() noexcept;

    /** @brief Starts the ESP32-S3 hardware timer. */
    void start() noexcept override;

    /** @brief Stops the ESP32-S3 hardware timer. */
    void stop() noexcept override;

    /** @brief Configures the alarm value based on milliseconds. 
     * @param[in] period_ms Period in milliseconds.
     */
    void setPeriod(std::uint32_t period_ms) noexcept override;

    /** @brief Check if the hardware timer has triggered an interrupt (timeout).
     * @return True if a timeout has occured, false otherwise.
     */
    bool isTimeout() noexcept override;

    
    /** @brief Check the initialization status of the GPTimer.
     * @return True if initialized, false otherwise.
     */
    bool isInitialized() const noexcept override;

private:
    /** @brief Driver handle for the ESP-IDF GPTimer instance. */
    gptimer_handle_t handle = nullptr;

    /** @brief Interrupt flag set by the ISR when the timer alarms. */
    volatile bool timeout_flag = false;

    /** @brief Flag tracking if the hardware timer is successfully initialized. */
    bool my_initialized = false;
    
    
     /** @brief ISR callback function triggered on timer alarm events. */
    static bool IRAM_ATTR timer_callback(gptimer_handle_t timer, 
                                        const gptimer_alarm_event_data_t *edata, 
                                        void *user_data);
};

} // namespace driver::timer
