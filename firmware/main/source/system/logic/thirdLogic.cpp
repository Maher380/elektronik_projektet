#include "system/logic/thirdLogic.h"

#include "esp_log.h"

namespace app::logic
{
void ThirdLogic::run(const std::atomic<bool>& stop) noexcept
{
    (void)stop;
    ESP_LOGE("THIRD", "Third car logic is not implemented; motor remains disabled");
}
} // namespace app::logic
