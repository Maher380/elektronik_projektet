#pragma once
#include "freertos/FreeRTOS.h"
#include <functional>
namespace host {
extern TickType_t ticks;
extern std::function<void()> afterTick;
}
inline TickType_t xTaskGetTickCount() { return host::ticks; }
inline void vTaskDelay(TickType_t delay) {
    if (host::afterTick) { host::afterTick(); }
    host::ticks += delay;
}
