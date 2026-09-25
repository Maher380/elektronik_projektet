/** @attention Uncomment DRIVER_TEST_MODE to run the local driver test code. */
// #define DRIVER_TEST_MODE

/** @attention Uncomment ODOMETER_TEST_MODE to run a minimal odometer-only test app. */
// #define ODOMETER_TEST_MODE

/** @attention Uncomment MOTOR_TEST_MODE to run a minimal A89301 BLDC motor test app. */
// #define MOTOR_TEST_MODE

/** @attention Uncomment A89301_CONFIG_MODE to read, change and save the A89301 settings over I2C. */
// #define A89301_CONFIG_MODE


#if defined(DRIVER_TEST_MODE) || defined(ODOMETER_TEST_MODE) || defined(MOTOR_TEST_MODE) || defined(A89301_CONFIG_MODE)

#include "test_app/test_app.h"

#else

#include "driver/factory/esp32s3.h"
#include "system/logic/logic.h"
#include <atomic>
#include "sdkconfig.h"

#if CONFIG_CNB_ENABLE_MQTT
// Existing sdkconfig files override sdkconfig.defaults. Fail compilation rather
// than booting the known undersized stack; menuconfig remains usable to fix it.
static_assert(CONFIG_ESP_MAIN_TASK_STACK_SIZE >= 8192,
              "CnB MQTT requires main task stack >= 8192 bytes. Set Component config > "
              "ESP System Settings > Main task stack size in idf.py menuconfig.");
#endif

#endif

extern "C" void app_main(void)
{
    #if defined(DRIVER_TEST_MODE)
    app::test_app::runDriverTest();
    #elif defined(ODOMETER_TEST_MODE)
    app::test_app::runOdometerTest();
    #elif defined(MOTOR_TEST_MODE)
    app::test_app::runMotorTest();
    #elif defined(A89301_CONFIG_MODE)
    app::test_app::runA89301ConfigTest();
    #else
    std::atomic<bool> stop{false};
    driver::factory::Esp32s3 factory;
    app::logic::Logic logic(factory);
    logic.run(stop);
    #endif
}
