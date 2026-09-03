# CnB Agent Guide

## Purpose

This file is the persistent working context for coding agents in this repository.
Read it together with `README.md` and
`documentation/collaboration_rules/definition_of_done.md` before making changes.

Keep this file current when the team accepts a new technical decision, changes
the agenda, assigns ownership, or verifies hardware. Do not record passwords,
Wi-Fi credentials, tokens, personal data, or unverified guesses here.

## Project Context

- Team: CnB (Crash and Burn), three students.
- Goal: build autonomous cars for a school project and competition.
- Required workflow: Git, GitHub pull requests, Jira tasks, review, and testing.
- Main firmware: C++ with ESP-IDF for Arduino Nano ESP32 (ESP32-S3).
- The first physical demo used the firmware from the SCRUM-35 work.
- The demo hardware used an L7805CV to supply the IR sensor. Preserve that fact
  in historical demo documentation even if the final design uses another
  regulator.

The repository is the source of truth for code and versioned documentation.
Jira is the source of truth for task status and acceptance criteria. A chat is
not a substitute for either.

## Repository Layout

- `firmware/`: ESP-IDF application, drivers, logic, and host-side tests.
- `hardware/kicad/`: KiCad project files and manufacturing design.
- `hardware/ltspice/`: LTspice simulations.
- `documentation/design_documents/`: system, parts, and wiring documentation.
- `documentation/diaries/`: individual project diaries.
- `documentation/collaboration_rules/`: team process and Definition of Done.

## Working Rules

1. Inspect `git status`, the active branch, relevant code, and nearby tests
   before editing.
2. Never work directly on `main`. Start from the latest available `origin/main`.
3. Use the exact Jira branch name supplied by the team. The current Jira style
   is `<JIRA-key>-<type>-<short-kebab-title>`, for example
   `SCRUM-56-fix-adc-support-for-multiple-ir-sensors`.
4. If no Jira task exists, use the README convention
   `<type>/<short-kebab-title>` and create/link a Jira task before substantial
   implementation work.
5. Valid types are `feature`, `fix`, `docs`, `experiment`, `refactor`, `test`,
   `chore`, `release`, and `ci`.
6. Keep commits atomic by concern. Use short English imperative messages and
   include the Jira key when applicable.
7. Do not mix unrelated cleanup into a task or modify another team member's
   work without coordination.
8. Preserve all existing user changes and untracked project files. Never reset,
   discard, move, or overwrite them unless explicitly instructed.
9. Before a commit or push, show the user the branch, status, changed files,
   relevant diff summary, and verification result.
10. Do not push, merge, force-push, create a remote tag, or move a tag without
    explicit user approval.
11. Never commit `firmware/build/`, generated ESP-IDF output, local `sdkconfig`
    credentials, editor state, tokens, or secrets.
12. Update wiring and design documentation whenever a software pin assignment
    or electrical requirement changes.

## Firmware Architecture

- Application logic depends on driver interfaces and receives drivers through
  the factory. Keep ESP-IDF-specific APIs in concrete ESP32-S3 drivers.
- Maintain both concrete ESP32-S3 implementations and host-test stubs when an
  interface changes.
- Keep ownership clear: ADC measures voltage, the IR sensor converts a reading
  into distance, the motor driver controls motion, and logic decides behavior.
- ADC1 must have one shared oneshot unit handle with several configured channel
  objects. Do not allocate ADC1 independently for every IR sensor.
- Validate all factory/interface signatures against their stubs and call sites.
- Invalid or unavailable sensor data must produce a safe motor state, not
  continued movement.
- New network behavior must not block or destabilize the control loop.

## Pin Map

`pin_mapping.md` is the source of truth for the
Arduino labels, ESP32-S3 GPIO/ADC mapping, and current project connections.
Verify it against `firmware/main/include/system/logic/logic.h`, the Arduino Nano
ESP32 pinout, the MP6550 board, and KiCad before wiring or changing code.

## Electrical Constraints

- The vehicle battery is approximately 7.2 V.
- All interconnected boards and sensors require a common ground.
- Sharp GP2Y0A21YK IR sensors require a regulated supply in their specified
  range. Never connect them directly to the 7.2 V battery.
- Place at least 10 uF of local supply capacitance near each IR sensor.
- The demo used an L7805CV. Its dropout margin from a loaded 7.2 V battery is
  small, so regulator choice must be reviewed for the final design.
- The Arduino Nano ESP32 may be supplied through VIN within the board's rated
  range. Do not assume VBUS provides 5 V when the board is powered from VIN.
- Treat the exact MP6550 module pinout and limits as authoritative only after
  checking its schematic or datasheet; module labels can differ.
- Do not energize hardware when code, wiring documentation, and the physical
  build disagree.

## Build And Verification

Run ESP-IDF commands from `firmware/` in an ESP-IDF-enabled shell:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

For every firmware change:

- Build for `esp32s3`.
- Run the relevant host tests and add focused tests for changed behavior.
- Report clearly whether verification was a host test, compile/build, flash,
  or physical hardware test. A successful build is not a hardware test.
- Verify all three sensor values and motor fail-safe behavior on hardware when
  sensor/control logic changes.

On Maher's Windows environment, Application Control may block a compiler child
process started from the Codex sandbox. If that exact policy error occurs, do
not spend excessive time reinstalling tools. Perform the checks available in
the workspace, provide the exact ESP-IDF command, and use the user's ESP-IDF
shell output as the final build/flash evidence.

## Current Agenda

Work through this list in order unless the user or Jira changes the priority.

1. **Three IR sensors / ADC (SCRUM-56):** implementation exists on
   `SCRUM-56-fix-adc-support-for-multiple-ir-sensors`. Confirm PR/merge status,
   then physically test all three sensors and fail-safe stopping before marking
   the task done.
2. **MQTT and Wi-Fi:** create a Jira task, then add MQTT telemetry and remote
   configuration. Reuse and adapt the MQTT drivers from
   `https://github.com/OliverEdman/cpp-driver-library-p02`, preserving license
   attribution. Keep the existing Wi-Fi support.
3. **MQTT scope:** publish the three distances and motor state; support validated
   remote updates for stop distance, speed, and logging interval; retain safe
   defaults and fail-safe behavior when networking fails; add stubs and tests.
4. **Demo release:** wait for the colleague's KiCad files, include the exact demo
   hardware and historically accurate wiring, then create the immutable
   `v1.0-demo` tag on that exact commit. Never add commits "to" an existing tag
   or silently move it.
5. **Final power design:** update wiring and KiCad with the IR regulator and
   decoupling. Record that the demo used L7805CV, while separately evaluating a
   regulator with sufficient margin for the final car.
6. **Later backlog:** dedicated MP6550 tests, CI, hall sensors, BoM maintenance,
   and Raspberry Pi 5/camera work for car 2 (Slammen).

## Team Boundaries

- A classmate currently owns the MP6550 driver work. Review interface impact and
  coordinate before editing that implementation.
- A classmate is preparing the demo KiCad files. Do not replace their files with
  placeholders or invent a final schematic while waiting.
- The original checkout may contain untracked BoM files under `hardware/`.
  Preserve them and check before moving work between branches or worktrees.

## Definition Of Done For Agent Work

Before presenting a task as complete:

- The implementation matches the Jira acceptance criteria.
- Relevant builds and tests pass, or blockers are stated precisely.
- Hardware-affecting changes include matching wiring/design documentation.
- No secrets, build output, or unrelated changes are included.
- The user has reviewed the final diff before any requested push or tag.
- This file is updated if the work changed a durable decision or the agenda.
