/** @file main.cpp @brief Start the selected car logic. */

/** @attention Uncomment DRIVER_TEST_MODE to run the local driver test code. */
// #define DRIVER_TEST_MODE

/** @attention Uncomment ODOMETER_TEST_MODE to run a minimal odometer-only test app. */
// #define ODOMETER_TEST_MODE

/** @attention Uncomment MOTOR_TEST_MODE to run a minimal A89301 BLDC motor test app. */
// #define MOTOR_TEST_MODE

/** @attention Uncomment A89301_CONFIG_MODE to read, change and save the A89301 settings over I2C. */
// #define A89301_CONFIG_MODE

// if compiling a test purpose variant
#if defined(DRIVER_TEST_MODE) || defined(ODOMETER_TEST_MODE) || defined(MOTOR_TEST_MODE) || defined(A89301_CONFIG_MODE)

#include "test_app/test_app.h"

#else

#include <atomic>
#include "driver/factory/esp32s3.h"
#include "sdkconfig.h"
#include "system/logic/target.h"

#endif // if compiling a test purpose variant

#if CONFIG_CNB_ENABLE_MQTT
static_assert(CONFIG_ESP_MAIN_TASK_STACK_SIZE >= 8192,
              "MQTT requires Main task stack size >= 8192 in menuconfig.");
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
    app::logic::TargetLogic logic{factory};
    logic.run(stop);
    #endif
}
