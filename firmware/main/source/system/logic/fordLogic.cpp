#include "system/logic/fordLogic.h"

#include "esp_log.h"

namespace app::logic
{
void FordLogic::run(const std::atomic<bool>& stop) noexcept
{
    (void)stop;
    ESP_LOGE("FORD", "Ford logic is not implemented; motor remains disabled");
}
} // namespace app::logic
