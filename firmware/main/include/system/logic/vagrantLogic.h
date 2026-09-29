#pragma once

#include <atomic>

namespace driver::factory { class Interface; }

namespace app::logic
{
class VagrantLogic final
{
public:
    explicit VagrantLogic(driver::factory::Interface& factory) noexcept : myFactory{factory} {}
    void run(const std::atomic<bool>& stop) noexcept;

private:
    driver::factory::Interface& myFactory;
};
} // namespace app::logic
