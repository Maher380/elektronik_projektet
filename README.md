# CnB - Crash and Burn

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
+-- CMakeLists.txt
+-- main/
|   +-- CMakeLists.txt
|   +-- Kconfig.projbuild
|   +-- include/
|   |   +-- driver/
|   |   +-- system/
|   +-- source/
|       +-- driver/
|       +-- system/
|       +-- main.cpp
```

## Build

Use the ESP-IDF shell to build and flash:

```bash
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
