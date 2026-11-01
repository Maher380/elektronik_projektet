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
	$(TEST_SRC_DIR)/test/pin_manager.cpp

SOURCE_FILES := \
	$(FIRMWARE_SRC_DIR)/system/pin_manager/esp32s3.cpp \
	$(FIRMWARE_SRC_DIR)/driver/distance_sensor/gp2y0a21yk.cpp \
	$(FIRMWARE_SRC_DIR)/driver/motor/a89301.cpp \
	$(FIRMWARE_SRC_DIR)/driver/motor/a89301_programmer.cpp \
	$(FIRMWARE_SRC_DIR)/driver/motor/l298n.cpp \
	$(FIRMWARE_SRC_DIR)/driver/motor/mp6550.cpp \
	$(FIRMWARE_SRC_DIR)/driver/serial/stub.cpp

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

.PHONY: navigation
navigation:
	@mkdir -p build
	@$(CC) -c ../managed_components/espressif__cjson/cJSON/cJSON.c -o build/navigation_cjson.o
	@$(CXX) -std=c++17 -Wall -Wextra -Werror -Wno-unused-const-variable -Wno-return-type -I../../tests/include -I$(FIRMWARE_INC_DIR) -I../managed_components/espressif__cjson/cJSON navigation_regression.cpp $(FIRMWARE_SRC_DIR)/system/communication/manager.cpp $(FIRMWARE_SRC_DIR)/system/runtime/control.cpp $(FIRMWARE_SRC_DIR)/driver/motor/mp6550.cpp $(FIRMWARE_SRC_DIR)/driver/servo/vagrant.cpp $(FIRMWARE_SRC_DIR)/driver/serial/stub.cpp build/navigation_cjson.o -o build/navigation_tests
	@./build/navigation_tests
