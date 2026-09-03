#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

#include "driver/adc/esp32s3.h"
#include "system/pin_manager/esp32s3.h"
#include "esp_adc/adc_cali_scheme.h"

namespace driver::adc 
{
namespace
{
constexpr adc_atten_t AdcAttenuation{ADC_ATTEN_DB_12};
constexpr adc_bitwidth_t AdcBitWidth{ADC_BITWIDTH_DEFAULT};

class SharedAdcUnit final
{
public:
    bool acquire(adc_oneshot_unit_handle_t& handle) noexcept
    {
        std::lock_guard<std::mutex> lock{myMutex};

        if (!myHandle)
        {
            adc_oneshot_unit_init_cfg_t unitConfig{};
            unitConfig.unit_id = ADC_UNIT_1;
            unitConfig.ulp_mode = ADC_ULP_MODE_DISABLE;

            if (adc_oneshot_new_unit(&unitConfig, &myHandle) != ESP_OK)
            {
                return false;
            }
        }

        ++myUsers;
        handle = myHandle;
        return true;
    }

    bool release(adc_oneshot_unit_handle_t handle) noexcept
    {
        std::lock_guard<std::mutex> lock{myMutex};

        if (!myHandle || (handle != myHandle) || (myUsers == 0U))
        {
            return false;
        }

        --myUsers;
        if (myUsers != 0U)
        {
            return true;
        }

        if (adc_oneshot_del_unit(myHandle) != ESP_OK)
        {
            ++myUsers;
            return false;
        }

        myHandle = nullptr;
        return true;
    }

private:
    std::mutex myMutex;
    adc_oneshot_unit_handle_t myHandle{nullptr};
    std::size_t myUsers{0U};
};

SharedAdcUnit& sharedAdcUnit() noexcept
{
    static SharedAdcUnit unit{};
    return unit;
}

// Singleton pin manager instance.
auto& myPinManager = sys::pin_manager::Esp32s3::instance();

} // namespace

Esp32s3::Esp32s3(std::uint8_t pin) noexcept
    : myState{false}
    , myPin{pin}
    , myChannel{ADC_CHANNEL_0}
    , myHandle{nullptr}
    , myCalibrationHandle{nullptr}
{}

Esp32s3::~Esp32s3() noexcept
{
    if (myState)
    {
        deinit();
    }
}

bool Esp32s3::isInitialized() const noexcept 
{
    return myState;
}
bool Esp32s3::init() noexcept
{
    // Return false if ADC is already initialized.
    if (myState)
    {
        return false;
    }

    adc_unit_t unit{ADC_UNIT_1};
    if ((adc_oneshot_io_to_channel(myPin, &unit, &myChannel) != ESP_OK) ||
        (unit != ADC_UNIT_1))
    {
        return false;
    }

    // Try to book a GPIO pin via the pin manager, return false on failure.
    if (!myPinManager.reservePin(myPin))
    {
        return false;
    }

    if (!sharedAdcUnit().acquire(myHandle))
    {
        myPinManager.releasePin(myPin);
        return false;
    }
    adc_oneshot_chan_cfg_t channelConfig = {};
    channelConfig.bitwidth = AdcBitWidth;
    channelConfig.atten = AdcAttenuation;

    // Release resources on configuration failure.
    if (adc_oneshot_config_channel(myHandle, myChannel, &channelConfig) != ESP_OK)
    {
        sharedAdcUnit().release(myHandle);
        myHandle = nullptr;
        myPinManager.releasePin(myPin);
        return false;
    }

    adc_cali_curve_fitting_config_t calibrationConfig{};
    calibrationConfig.unit_id = ADC_UNIT_1;
    calibrationConfig.chan = myChannel;
    calibrationConfig.atten = AdcAttenuation;
    calibrationConfig.bitwidth = AdcBitWidth;

    if (adc_cali_create_scheme_curve_fitting(&calibrationConfig, &myCalibrationHandle) != ESP_OK)
    {
        sharedAdcUnit().release(myHandle);
        myHandle = nullptr;
        myPinManager.releasePin(myPin);
        return false;
    }

    myState = true;
    return true;
}


bool Esp32s3::deinit() noexcept 
{
    // Return false if init() never succeeded.
    if ( !myState ) { return false; }
    
    if (myCalibrationHandle &&
        (adc_cali_delete_scheme_curve_fitting(myCalibrationHandle) != ESP_OK))
    {
        return false;
    }
    myCalibrationHandle = nullptr;

    if (!sharedAdcUnit().release(myHandle))
    {
        return false;
    }

    myHandle = nullptr;
    myPinManager.releasePin(myPin);

    myState = false;
    return true;
}

std::uint16_t Esp32s3::readRaw() const noexcept
{
    // Check if ADC is initiated, return 0 on failure.
    if (!myState) { return 0U; }

    // Create an int to match ESP-IDF preference.
    int rawValue{0U};

    // Read raw ADC value, return 0 on failure.
    if(adc_oneshot_read(myHandle, myChannel, &rawValue) != ESP_OK)
    {
        return 0U;
    }
    return static_cast<std::uint16_t>(rawValue);
}

float Esp32s3::readVoltage() const noexcept
{
    if (!myState || !myCalibrationHandle)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    int rawValue{0};
    if (adc_oneshot_read(myHandle, myChannel, &rawValue) != ESP_OK)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    int voltageMillivolts{0};
    if (adc_cali_raw_to_voltage(myCalibrationHandle, rawValue, &voltageMillivolts) != ESP_OK)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }

    return static_cast<float>(voltageMillivolts) / 1000.0F;
}

} // namespace driver::adc 
