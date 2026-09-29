/** @file main.cpp @brief Start the selected car logic. */
#include <atomic>

#include "driver/factory/esp32s3.h"
#include "sdkconfig.h"
#include "system/logic/target.h"

#if CONFIG_CNB_ENABLE_MQTT
static_assert(CONFIG_ESP_MAIN_TASK_STACK_SIZE >= 8192,
              "MQTT requires Main task stack size >= 8192 in menuconfig.");
#endif

extern "C" void app_main(void)
{
    std::atomic<bool> stop{false};
    driver::factory::Esp32s3 factory;
    app::logic::TargetLogic logic{factory};
    logic.run(stop);
}
