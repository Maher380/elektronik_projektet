cmake_minimum_required(VERSION 3.16)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(cnb_scrum16_mqtt_experiment)



add_compile_options(-std=c++17)
