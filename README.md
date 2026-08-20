# 🔥 CnB - Crash and Burn

Autonomous car school project built with C++ and ESP-IDF for the ESP32-S3.

This repository is used by team **CnB (Crash and Burn)** to develop the
software for an autonomous competition car.

The project starts from an ESP32-S3 driver library and includes:

- hardware abstraction through driver interfaces
- ESP32-S3 implementations for hardware-level code
- stubs for testing without hardware
- factory-based driver creation
- ESP-IDF CMake project structure

## Project Layout

```text
.
+-- 📁 firmware/                  ESP-IDF firmware project for the ESP32-S3
|   +-- CMakeLists.txt             Firmware project entry point
|   +-- 📁 main/                   Main ESP-IDF component
|       +-- CMakeLists.txt         Component source list and dependencies
|       +-- Kconfig.projbuild      Project configuration used by menuconfig
|       +-- 📁 include/            Public C++ headers
|       |   +-- 📁 driver/         Hardware driver interfaces and implementations
|       |   +-- 📁 system/         System-level interfaces and logic headers
|       +-- 📁 source/             C++ source files
|           +-- 📁 driver/         ADC, GPIO, timer, WiFi and serial drivers
|           +-- 📁 system/         Application logic and pin manager
|           +-- main.cpp           ESP-IDF application entry point
+-- 📁 hardware/                  Hardware design files
|   +-- 📁 kicad/                  KiCad schematics, PCB layout and exports
|   +-- 📁 ltspice/                LTspice simulations and component models
+-- README.md                      Project overview and workflow rules
+-- THIRD_PARTY_NOTICES.md         Attribution for imported starter code
```

The repository is split into two main areas:

- 📁 `firmware/` contains all software and embedded code.
- 📁 `hardware/` contains electronics design files, simulations and exports.

## Build

Use the ESP-IDF shell to build and flash:

```bash
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Serial output is enabled over USB for later sensor testing. The application
prints `CnB serial ready` after the serial driver starts.

WiFi settings are configured with:

```bash
idf.py menuconfig
```

Open `Autonomous car network configuration` and set SSID and password. Do not
commit generated `sdkconfig` files with private credentials.

## Hardware

Hardware files are stored in `hardware/`.

- 📁 `hardware/kicad/` - KiCad project files for schematics, PCB layout, symbols, footprints and generated exports.
- 📁 `hardware/ltspice/` - LTspice simulation files, component models and circuit experiments.

## Branch Naming

Do not work directly on `main`. Create a branch for every task.

Branch names must use this format:

```text
<type>/<short-english-title>
```

Use a short English title. Prefer lowercase words separated with hyphens.

Allowed branch types:

- `feature/` - new functionality
- `fix/` - fixes for broken behavior
- `docs/` - documentation-only work
- `experiment/` - exploratory work not meant to be permanent
- `refactor/` - internal code cleanup without changing behavior
- `test/` - test-only changes
- `chore/` - maintenance, tooling or dependency updates
- `release/` - preparing a release
- `ci/` - pipeline or automation changes

Examples:

```bash
git switch -c docs/create-diary-structure
git switch -c feature/support-ir-sensor
```

If a branch belongs to a Jira task, include the Jira key in commits and pull
request titles:

```bash
git commit -m "CAR-12 support IR sensor"
```

## Pull Requests

Push your branch and open a pull request into `main`.

Each pull request should include:

- what was changed
- how it was tested
- the related Jira task, if one exists

## Attribution

This project includes code derived from
`OliverEdman/cpp-driver-library-p02`, licensed under MIT. See
`THIRD_PARTY_NOTICES.md`.
