/**
 * @file target.h
 * @brief The car class of the target car chosen in Kconfig.
 */

#pragma once

#include "sdkconfig.h"

#if CONFIG_CNB_CAR_VAGRANT
#include "system/car/vagrant.h"
namespace app::car { using Target = Vagrant; }
#elif CONFIG_CNB_CAR_FORD
#include "system/car/ford.h"
namespace app::car { using Target = Ford; }
#else
#error "No target car selected. Set Autonomous car target > Target car in idf.py menuconfig."
#endif
