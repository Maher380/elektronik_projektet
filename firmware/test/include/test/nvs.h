#pragma once

namespace driver::nvs
{
class Interface;
} // namespace driver::nvs

namespace test
{
bool runNvsTest(driver::nvs::Interface& nvs) noexcept;
} // namespace test
