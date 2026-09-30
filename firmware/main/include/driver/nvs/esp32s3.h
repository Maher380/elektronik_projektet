/**
 * @file esp32s3.h
 * @brief NVS driver for ESP32-S3, using the default NVS partition.
 */

#pragma once

#include <cstddef>
#include <memory>

#include "driver/nvs/interface.h"

namespace driver::nvs
{

class Esp32s3 final : public Interface
{
public:
    Esp32s3() noexcept = default;
    ~Esp32s3() noexcept override = default;

    bool init() noexcept override;
    bool isInitialized() const noexcept override;
    std::unique_ptr<Handle> open(Namespace ns) noexcept override;

    Esp32s3(const Esp32s3&)            = delete;
    Esp32s3& operator=(const Esp32s3&) = delete;
    Esp32s3(Esp32s3&&)                 = delete;
    Esp32s3& operator=(Esp32s3&&)      = delete;

private:
    /** True while a handle to that namespace exists. */
    bool myOpen[static_cast<std::size_t>(Namespace::Count)]{};

    bool myInitialized{false};
};

} // namespace driver::nvs
