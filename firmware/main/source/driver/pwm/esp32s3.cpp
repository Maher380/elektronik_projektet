/**
 * @file esp32s3.cpp
 * @brief ESP32-S3 MCPWM based PWM driver implementation.
 */

#include "driver/pwm/esp32s3.h"

#include "system/pin_manager/esp32s3.h"

#include "esp_err.h"

namespace driver::pwm
{
namespace
{
/** Shared pin manager used to reserve the PWM output GPIO. */
auto& myPinManager = sys::pin_manager::Esp32s3::instance();

/**
 * @brief Check if a duty cycle is inside the supported PWM range.
 *
 * @param[in] duty Duty cycle to validate.
 * @return True if duty is between 0.0f and 1.0f.
 */
bool isDutyValid(const float duty) noexcept
{
    return (duty >= 0.0F) && (duty <= 1.0F);
}

/**
 * @brief Check whether a PWM configuration can produce a valid period.
 *
 * @param[in] config PWM configuration to validate.
 * @return True if frequency and resolution are usable.
 */
bool isConfigValid(const Config& config) noexcept
{
    return (config.frequencyHz > 0U) && (config.resolutionHz >= config.frequencyHz);
}

/**
 * @brief Convert frequency and resolution to MCPWM period ticks.
 *
 * @param[in] config PWM configuration.
 * @return Number of ticks in one PWM period.
 */
std::uint32_t periodTicksFromConfig(const Config& config) noexcept
{
    return config.resolutionHz / config.frequencyHz;
}

/**
 * @brief Convert a normalized duty cycle to MCPWM compare ticks.
 *
 * @param[in] duty Duty cycle in range 0.0f - 1.0f.
 * @param[in] periodTicks Number of ticks in one PWM period.
 * @return Comparator value for the requested duty cycle.
 */
std::uint32_t compareTicksFromDuty(const float duty, const std::uint32_t periodTicks) noexcept
{
    return static_cast<std::uint32_t>((static_cast<float>(periodTicks) * duty) + 0.5F);
}
} // namespace

Esp32s3::Esp32s3(Config config) noexcept
    : myConfig{config}
    , myPeriodTicks{periodTicksFromConfig(config)}
    , myDuty{0.0F}
    , myIsInitialized{false}
    , myPinReserved{false}
    , myTimerEnabled{false}
    , myTimer{nullptr}
    , myOperator{nullptr}
    , myComparator{nullptr}
    , myGenerator{nullptr}
{}

Esp32s3::Esp32s3(std::uint8_t pin) noexcept
    : Esp32s3{Config{pin}}
{}

Esp32s3::~Esp32s3() noexcept
{
    releaseResources();
}

bool Esp32s3::init() noexcept
{
    if (myIsInitialized || !isConfigValid(myConfig))
    {
        return false;
    }

    myPeriodTicks = periodTicksFromConfig(myConfig);
    if (myPeriodTicks == 0U)
    {
        return false;
    }

    if (!myPinManager.reservePin(myConfig.pin))
    {
        return false;
    }
    myPinReserved = true;

    mcpwm_timer_config_t timerConfig{};
    timerConfig.group_id = static_cast<int>(myConfig.groupId);
    timerConfig.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
    timerConfig.resolution_hz = myConfig.resolutionHz;
    timerConfig.period_ticks = myPeriodTicks;
    timerConfig.count_mode = MCPWM_TIMER_COUNT_MODE_UP;

    if (mcpwm_new_timer(&timerConfig, &myTimer) != ESP_OK)
    {
        releaseResources();
        return false;
    }

    mcpwm_operator_config_t operatorConfig{};
    operatorConfig.group_id = static_cast<int>(myConfig.groupId);

    if (mcpwm_new_operator(&operatorConfig, &myOperator) != ESP_OK)
    {
        releaseResources();
        return false;
    }

    if (mcpwm_operator_connect_timer(myOperator, myTimer) != ESP_OK)
    {
        releaseResources();
        return false;
    }

    mcpwm_comparator_config_t comparatorConfig{};
    comparatorConfig.flags.update_cmp_on_tez = true;

    if (mcpwm_new_comparator(myOperator, &comparatorConfig, &myComparator) != ESP_OK)
    {
        releaseResources();
        return false;
    }

    mcpwm_generator_config_t generatorConfig{};
    generatorConfig.gen_gpio_num = static_cast<int>(myConfig.pin);
    generatorConfig.flags.invert_pwm = myConfig.invertOutput;

    if (mcpwm_new_generator(myOperator, &generatorConfig, &myGenerator) != ESP_OK)
    {
        releaseResources();
        return false;
    }

    if (mcpwm_generator_set_action_on_timer_event(
            myGenerator,
            MCPWM_GEN_TIMER_EVENT_ACTION(
                MCPWM_TIMER_DIRECTION_UP,
                MCPWM_TIMER_EVENT_EMPTY,
                MCPWM_GEN_ACTION_HIGH)) != ESP_OK)
    {
        releaseResources();
        return false;
    }

    if (mcpwm_generator_set_action_on_compare_event(
            myGenerator,
            MCPWM_GEN_COMPARE_EVENT_ACTION(
                MCPWM_TIMER_DIRECTION_UP,
                myComparator,
                MCPWM_GEN_ACTION_LOW)) != ESP_OK)
    {
        releaseResources();
        return false;
    }

    if (!applyDuty(myDuty))
    {
        releaseResources();
        return false;
    }

    if (mcpwm_timer_enable(myTimer) != ESP_OK)
    {
        releaseResources();
        return false;
    }
    myTimerEnabled = true;

    if (mcpwm_timer_start_stop(myTimer, MCPWM_TIMER_START_NO_STOP) != ESP_OK)
    {
        releaseResources();
        return false;
    }

    myIsInitialized = true;
    return true;
}

bool Esp32s3::deinit() noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    return releaseResources();
}

bool Esp32s3::isInitialized() const noexcept
{
    return myIsInitialized;
}

bool Esp32s3::setDuty(const float duty) noexcept
{
    if (!myIsInitialized)
    {
        return false;
    }

    return applyDuty(duty);
}

float Esp32s3::duty() const noexcept
{
    return myDuty;
}

std::uint32_t Esp32s3::frequencyHz() const noexcept
{
    return myConfig.frequencyHz;
}

bool Esp32s3::setFrequencyHz(const std::uint32_t frequencyHz) noexcept
{
    if (frequencyHz == 0U || frequencyHz > myConfig.resolutionHz)
    {
        return false;
    }

    const auto newPeriodTicks = myConfig.resolutionHz / frequencyHz;
    if (newPeriodTicks == 0U)
    {
        return false;
    }

    if (myIsInitialized && mcpwm_timer_set_period(myTimer, newPeriodTicks) != ESP_OK)
    {
        return false;
    }

    myConfig.frequencyHz = frequencyHz;
    myPeriodTicks = newPeriodTicks;
    if (myIsInitialized)
    {
        return applyDuty(myDuty);
    }

    return true;
}

bool Esp32s3::applyDuty(const float duty) noexcept
{
    if (!isDutyValid(duty) || (myGenerator == nullptr) || (myComparator == nullptr))
    {
        return false;
    }

    if (duty <= 0.0F)
    {
        if (mcpwm_generator_set_force_level(myGenerator, 0, true) != ESP_OK)
        {
            return false;
        }
    }
    else if (duty >= 1.0F)
    {
        if (mcpwm_generator_set_force_level(myGenerator, 1, true) != ESP_OK)
        {
            return false;
        }
    }
    else
    {
        if (mcpwm_generator_set_force_level(myGenerator, -1, true) != ESP_OK)
        {
            return false;
        }

        const auto compareTicks = compareTicksFromDuty(duty, myPeriodTicks);
        if (mcpwm_comparator_set_compare_value(myComparator, compareTicks) != ESP_OK)
        {
            return false;
        }
    }

    myDuty = duty;
    return true;
}

bool Esp32s3::releaseResources() noexcept
{
    bool success = true;

    if (myTimer != nullptr)
    {
        if (mcpwm_timer_start_stop(myTimer, MCPWM_TIMER_STOP_EMPTY) != ESP_OK)
        {
            success = false;
        }
    }

    if (myGenerator != nullptr)
    {
        if (mcpwm_del_generator(myGenerator) != ESP_OK)
        {
            success = false;
        }
        myGenerator = nullptr;
    }

    if (myComparator != nullptr)
    {
        if (mcpwm_del_comparator(myComparator) != ESP_OK)
        {
            success = false;
        }
        myComparator = nullptr;
    }

    if (myOperator != nullptr)
    {
        if (mcpwm_del_operator(myOperator) != ESP_OK)
        {
            success = false;
        }
        myOperator = nullptr;
    }

    if (myTimerEnabled && (myTimer != nullptr))
    {
        if (mcpwm_timer_disable(myTimer) != ESP_OK)
        {
            success = false;
        }
        myTimerEnabled = false;
    }

    if (myTimer != nullptr)
    {
        if (mcpwm_del_timer(myTimer) != ESP_OK)
        {
            success = false;
        }
        myTimer = nullptr;
    }

    if (myPinReserved)
    {
        myPinManager.releasePin(myConfig.pin);
        myPinReserved = false;
    }

    myIsInitialized = false;
    myDuty = 0.0F;
    return success;
}

} // namespace driver::pwm
