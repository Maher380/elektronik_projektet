# Verification — 2026-09-10

- The source project stayed unchanged: 114 file hashes plus Git status verified.
- Eleven original Logic methods match SCRUM-16 verbatim. The four driving
  methods differ only by the configurable stop-distance and duty values.
- Motor, PWM, IR and servo source match SCRUM-16. Wi-Fi/MQTT drivers and the
  communication Manager match SCRUM-50.
- All 20 firmware C++ translation units passed ESP32-S3 compiler syntax checks
  using the existing SCRUM-50 SDK configuration. This does not link firmware.
- Eleven host integration scenarios passed using the actual `Logic::run()`:
  boot disarmed; route choice, braking and recovery; GradualSweep and stop;
  rejection of duty above 1; rejection of invalid stop distance; changed stop
  distance and duty 0/0.8/1; SlowLeft configured duty; SlowRight configured duty
  and obstacle; heartbeat expiry; MQTT disconnect; ADC telemetry mapping.
- PowerShell scripts passed syntax parsing. Isolated parameter binding accepted
  duty 0, 0.75 and 1, and rejected -0.01 and 1.01. No MQTT command was published.
- Full ESP-IDF build was unavailable: launching the installed ESP-IDF Python
  returned “Access is denied.” No build environment or system permissions were changed.
- No flash, physical motor/servo test, live start command, Git commit or push
  was performed. Full build/link, partition-size check and hardware validation
  remain user-side steps in the guide.

The tests do not establish that sensor readings, motor power or steering
calibration are correct on the physical car.
