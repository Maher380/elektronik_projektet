CMAKE ?= cmake
CTEST ?= ctest
CJSON_DIR ?= ../managed_components/espressif__cjson/cJSON

.PHONY: default build run clean
default: build run

build:
	$(CMAKE) -S . -B build -DCJSON_DIR="$(abspath $(CJSON_DIR))"
	$(CMAKE) --build build

run:
	$(CTEST) --test-dir build -C Debug --output-on-failure

clean:
	$(CMAKE) --build build --target clean
