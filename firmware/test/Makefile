TARGET := build/unit_tests

FIRMWARE_INC_DIR := ../main/include
FIRMWARE_SRC_DIR := ../main/source

TEST_INC_DIR := include
TEST_SRC_DIR := source

CXX ?= g++
CXX_FLAGS := -Wall -Wextra -Werror -std=c++17 -I$(FIRMWARE_INC_DIR) -I$(TEST_INC_DIR)

TEST_FILES := \
	$(TEST_SRC_DIR)/test.cpp \
	$(TEST_SRC_DIR)/test/a89301_programmer.cpp \
	$(TEST_SRC_DIR)/test/drive_style.cpp \
	$(TEST_SRC_DIR)/test/gap_calibration.cpp \
	$(TEST_SRC_DIR)/test/nvs.cpp \
	$(TEST_SRC_DIR)/test/odometer_gaps.cpp \
	$(TEST_SRC_DIR)/test/pi_link.cpp \
	$(TEST_SRC_DIR)/test/pin_manager.cpp \
	$(TEST_SRC_DIR)/test/servo.cpp \
	$(TEST_SRC_DIR)/test/speed_calibration.cpp \
	$(TEST_SRC_DIR)/test/start_module.cpp \
	$(TEST_SRC_DIR)/test/temperature_sensor.cpp \
	$(TEST_SRC_DIR)/test/voltage_meter.cpp \
	$(TEST_SRC_DIR)/test/wifi_store.cpp

SOURCE_FILES := \
	$(FIRMWARE_SRC_DIR)/system/pin_manager/esp32s3.cpp \
	$(FIRMWARE_SRC_DIR)/system/runtime/control.cpp \
	$(FIRMWARE_SRC_DIR)/system/communication/pi_link.cpp \
	$(FIRMWARE_SRC_DIR)/system/logic/gapCalibration.cpp \
	$(FIRMWARE_SRC_DIR)/system/logic/speedCalibration.cpp \
	$(FIRMWARE_SRC_DIR)/driver/servo/ford.cpp \
	$(FIRMWARE_SRC_DIR)/driver/servo/mg90s.cpp \
	$(FIRMWARE_SRC_DIR)/driver/distance_sensor/gp2y0a21yk.cpp \
	$(FIRMWARE_SRC_DIR)/driver/motor/a89301.cpp \
	$(FIRMWARE_SRC_DIR)/driver/motor/a89301_programmer.cpp \
	$(FIRMWARE_SRC_DIR)/driver/motor/l298n.cpp \
	$(FIRMWARE_SRC_DIR)/driver/odometer/gaps.cpp \
	$(FIRMWARE_SRC_DIR)/driver/odometer/store.cpp \
	$(FIRMWARE_SRC_DIR)/driver/serial/stub.cpp \
	$(FIRMWARE_SRC_DIR)/driver/start_module/gpio.cpp \
	$(FIRMWARE_SRC_DIR)/driver/temperature_sensor/tmp36.cpp \
	$(FIRMWARE_SRC_DIR)/driver/voltage_meter/divider.cpp \
	$(FIRMWARE_SRC_DIR)/driver/wifi/store.cpp

ALL_FILES := $(TEST_FILES) $(SOURCE_FILES)

.PHONY: default build run clean

default: build run

build:
	@mkdir -p build
	@$(CXX) $(ALL_FILES) -o $(TARGET) $(CXX_FLAGS)

run:
	@./$(TARGET)

clean:
	@rm -rf build
