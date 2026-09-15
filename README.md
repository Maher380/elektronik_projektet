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
- MQTT telemetry, runtime configuration and guarded start/stop control

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
|           +-- 📁 driver/         ADC, GPIO, MQTT, PWM, WiFi and other drivers
|           +-- 📁 system/         Logic, communication, runtime and pin manager
|           +-- main.cpp           ESP-IDF application entry point
+-- 📁 documentation/             Design decisions and operating instructions
|   +-- 📁 design_documents/       MQTT, wiring and other subsystem designs
+-- 📁 hardware/                  Hardware design files
|   +-- 📁 kicad/                  KiCad schematics, PCB layout and exports
|   +-- 📁 ltspice/                LTspice simulations and component models
+-- README.md                      Project overview and workflow rules
+-- THIRD_PARTY_NOTICES.md         Attribution for imported starter code
+-- 📁 tools/mqtt/                 PowerShell tools for operating the car
```

The repository is organized into four main areas:

- 📁 `firmware/` contains all software and embedded code.
- 📁 `hardware/` contains electronics design files, simulations and exports.
- 📁 `documentation/` contains design and collaboration documents.
- 📁 `tools/` contains development and operator utilities.

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

Open `Autonomous car network configuration` and set Wi-Fi plus MQTT values. The
broker URI must use the computer's LAN address, for example
`mqtt://BROKER_HOST_OR_IP:1883`, and not `localhost`. MQTT also requires a main-task
stack of at least 8192 bytes. `firmware/sdkconfig.defaults` supplies this for new
setups; for an existing `sdkconfig`, set **Component config → ESP System Settings
→ Main task stack size** to **8192** in `menuconfig`. The build rejects smaller
MQTT stacks. Check serial `main_stack_min` after MQTT traffic and reconnection. Do not commit generated
`sdkconfig` files with private credentials.

ESP-IDF downloads the managed `espressif/mqtt` component during the first
configure or build. The car boots disarmed and requires a fresh MQTT `start`
command before the motor can move.

Application MQTT topics are listed under `CommunicationTopics` near the top of
`firmware/main/source/system/logic/logic.cpp`. Published and subscribed topics
are separated there; set an existing entry to `nullptr` to disable it. Message
parsing, validation, connection retries and publication remain encapsulated in
the communication manager.

Telemetry includes raw ADC counts for all three distance sensors, sampled with
their distance readings. `watch-telemetry.ps1` displays readable columns;
`-Raw` displays the original JSON. NDJSON logging preserves full precision.

The MQTT integration keeps SCRUM-16 autonomous steering. All sensors must be
valid, and runtime limits apply to the selected path. See the MQTT document for
normal navigation versus test-style stopping, host tests and remaining physical
verification. MQTT configuration also accepts optional `driver_style`; the
PowerShell tool exposes `-DriverStyle DecideAction`, `SlowLeft`, `SlowRight` or
`GradualSweep`. Stop and confirm disarmed before changing modes, then check the
car's `config/state` response. This extension requires updated firmware; after
that flash, mode changes use MQTT. See section 8.4 of the guide below for the
full workflow and retained-configuration behavior.

See [MQTT telemetry and runtime control](documentation/design_documents/mqtt.md)
for Mosquitto setup, topics, safety behavior and operator commands.

For a Swedish walkthrough from building and flashing to Wi-Fi setup, MQTT
operation and locating parameters in the code, follow
[Från bygge och flashning till Wi-Fi och MQTT](documentation/guides/mqtt_steg_for_steg.md).

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
