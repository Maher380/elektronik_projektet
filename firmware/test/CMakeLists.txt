cmake_minimum_required(VERSION 3.16)
project(cnb_host_tests LANGUAGES C CXX)
set(CJSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/espressif__cjson/cJSON" CACHE PATH "Directory containing cJSON.c and cJSON.h")
if(NOT EXISTS "${CJSON_DIR}/cJSON.c")
    message(FATAL_ERROR "Set CJSON_DIR to a cJSON checkout (v1.7.19), or resolve ESP-IDF dependencies first.")
endif()
add_library(cjson_host OBJECT "${CJSON_DIR}/cJSON.c")
target_include_directories(cjson_host PUBLIC "${CJSON_DIR}")
set(SOURCE_DIR ../main/source)
add_executable(unit_tests
    source/test.cpp source/test/pin_manager.cpp source/test/runtime_control.cpp
    source/test/mqtt_integration.cpp
    ${SOURCE_DIR}/system/pin_manager/esp32s3.cpp
    ${SOURCE_DIR}/system/runtime/control.cpp ${SOURCE_DIR}/system/runtime/output.cpp
    ${SOURCE_DIR}/system/navigation/planner.cpp ${SOURCE_DIR}/system/communication/manager.cpp
    ${SOURCE_DIR}/driver/ir_sensor/esp32s3.cpp ${SOURCE_DIR}/driver/motor/l298n.cpp
    ${SOURCE_DIR}/driver/motor/mp6550.cpp ${SOURCE_DIR}/driver/servo/vagrant.cpp
    ${SOURCE_DIR}/driver/serial/stub.cpp)
target_compile_features(unit_tests PRIVATE cxx_std_17)
target_include_directories(unit_tests PRIVATE include ../main/include)
target_link_libraries(unit_tests PRIVATE cjson_host)
if(MSVC)
    target_compile_options(unit_tests PRIVATE /W4 /WX)
else()
    target_compile_options(unit_tests PRIVATE -Wall -Wextra -Werror)
endif()
enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
