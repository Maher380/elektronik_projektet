TARGET := build/unit_tests

FIRMWARE_INC_DIR := ../main/include
FIRMWARE_SRC_DIR := ../main/source

TEST_INC_DIR := include
TEST_SRC_DIR := source

CXX ?= g++
CXX_FLAGS := -Wall -Wextra -Werror -std=c++17 -I$(FIRMWARE_INC_DIR) -I$(TEST_INC_DIR)

TEST_FILES := \
	$(TEST_SRC_DIR)/test.cpp \
	$(TEST_SRC_DIR)/test/pin_manager.cpp

SOURCE_FILES := \
	$(FIRMWARE_SRC_DIR)/system/pin_manager/esp32s3.cpp \
	$(FIRMWARE_SRC_DIR)/driver/ir_sensor/esp32s3.cpp \
	$(FIRMWARE_SRC_DIR)/driver/motor/l298n.cpp \
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
