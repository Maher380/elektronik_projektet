#pragma once

#include "sdkconfig.h"

#if defined(CONFIG_CNB_CAR_VAGRANT)
#include "system/logic/vagrantLogic.h"
namespace app::logic { using TargetLogic = VagrantLogic; }
#elif defined(CONFIG_CNB_CAR_FORD)
#include "system/logic/fordLogic.h"
namespace app::logic { using TargetLogic = FordLogic; }
#elif defined(CONFIG_CNB_CAR_THIRD)
#include "system/logic/thirdLogic.h"
namespace app::logic { using TargetLogic = ThirdLogic; }
#else
#error "Select a target car in menuconfig."
#endif
