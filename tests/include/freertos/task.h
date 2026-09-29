/** @file task.h @brief Simulated task clock for system-loop tests. */
#pragma once
#include "freertos/FreeRTOS.h"
#include <functional>
namespace host {
extern TickType_t ticks;
extern std::function<void()> afterTick;
}
/** Return simulated elapsed ticks. */
inline TickType_t xTaskGetTickCount() { return host::ticks; }
/** Run the test callback and advance simulated time without sleeping. */
inline void vTaskDelay(TickType_t delay) {
    if (host::afterTick) { host::afterTick(); }
    host::ticks += delay;
}
