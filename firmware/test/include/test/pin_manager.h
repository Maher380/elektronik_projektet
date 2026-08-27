#pragma once

namespace sys::pin_manager
{
class Interface;
} // namespace sys::pin_manager

namespace test
{
bool runPinManagerTest(sys::pin_manager::Interface& manager) noexcept;
} // namespace test
