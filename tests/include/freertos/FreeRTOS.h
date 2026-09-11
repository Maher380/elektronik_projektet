#pragma once
#include <cstdint>
using TickType_t = std::uint32_t;
#define portTICK_PERIOD_MS 1U
#define pdMS_TO_TICKS(value) (value)
