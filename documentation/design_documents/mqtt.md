# MQTT Telemetry and Runtime Control

Status: SCRUM-50 integrated with SCRUM-16 on main `83154ef`. Host and isolated
Mosquitto tests pass. On 2026-09-08 the user flashed the driver-style extension;
logs show repeated main-task stack overflow immediately after Wi-Fi obtains an
IP address. The fix increases the main stack from 3584 to 8192 bytes and adds
runtime stack-margin logging. A fresh build/flash and hardware retest of this
fix are required; ESP-IDF Python is blocked in the agent environment.

## Scope

For practical setup and operation in Swedish, see the
[step-by-step guide](../guides/mqtt_steg_for_steg.md), including build, flash,
Wi-Fi, broker setup, MQTT commands and links to the relevant code.

This document defines MQTT communication for the CnB Vagrant car. The ESP32-S3
is flashed once, installed in the car, and then configured, started, stopped,
and monitored from the development computer without reflashing or serial input.

The initial broker is Mosquitto running on the Windows development computer on
the same private 2.4 GHz Wi-Fi network as the car. Raspberry Pi and other cars
are outside the scope of SCRUM-50.

## Safety Rules

- The car always boots disarmed and with zero motor output.
- MQTT may permit motion, but local logic always decides whether motion is safe.
- Wi-Fi or MQTT failure must not block the 20 Hz vehicle control loop.
- Loss of MQTT or the control heartbeat disarms the car.
- Reconnecting never rearms the car; a new `start` command is required.
- Invalid IR data inhibits motor output.
- SCRUM-16 normal navigation selects the clearest direction. Only an obstacle
  in that selected path inhibits motion; a nearby obstacle on another side does
  not prevent turning toward a clear side. Every sensor must still be valid.
- The slow-left, slow-right and sweep test styles retain their more conservative
  stop for an obstacle at any sensor. Motion may resume while still armed.
- An actuator error disarms and latches `actuator_fault` until reboot. The bridge
  is disabled through nSLEEP and both PWM outputs are commanded to zero.
- Changing `driver_style` requires disarmed; zero duty or inhibited motion while
  armed does not permit a mode change. Configuration never arms the car.
- MQTT start/stop is operational control, not an emergency-stop system. A
  physical power cutoff must remain available during testing.

## Architecture

```text
PowerShell tools / future dashboard
                 |
                 | MQTT over TCP/IP
                 v
       Mosquitto on Windows
                 |
                 | 2.4 GHz Wi-Fi
                 v
  ESP32-S3 CommunicationManager
                 |
                 | bounded, thread-safe messages
                 v
         20 Hz vehicle control loop
                 |
                 v
       IR sensors and motor driver
```

`app::communication::Manager` owns Wi-Fi and MQTT connection progress, retry
state, topic routing, JSON parsing and MQTT publications. `app::logic::Logic`
only advances that manager, supplies telemetry snapshots and consumes the
resulting runtime state. The MQTT event callback never controls the motor
directly. It copies complete incoming messages into a bounded FreeRTOS queue.
The communication manager consumes up to eight messages per control-loop
iteration, validates their JSON, and updates runtime state between sensor reads
and motor decisions.
If a control queue cannot accept a message, the failure must result in a safe
disarmed state rather than silently losing a stop condition.

Telemetry is produced from a coherent state snapshot. JSON parsing and
serialization use messages of at most 767 payload bytes plus a terminating NUL.
JSON nesting is limited to four levels before cJSON runs, and trailing data,
embedded NULs and escaped NULs are rejected. Actual network calls run in a
dedicated publisher task and ESP-MQTT's task. The control task only copies
outgoing messages to fixed-size FreeRTOS queues with zero wait:

- Eight incoming messages, up to eight consumed per control tick.
- One latest telemetry sample, overwritten by newer data. Samples waiting for
  one second or crossing a disconnect are discarded. QoS 0 telemetry is sent
  directly by the publisher and never enters the retransmission outbox.
- Eight queued QoS 1 state messages, plus at most one pending in the publisher.
  ESP-MQTT's outbox limit is configured to 4096 bytes. The worker retries a full
  outbox; the manager retains the latest state if its queue is full.
- A 4096-byte publisher stack and a one-second network operation timeout.

The main task runs `Logic` and synchronous JSON/queue handling. Its stack must
be at least 8192 bytes (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`). `sdkconfig.defaults`
sets this for new configurations; existing configurations must be updated via
menuconfig. The compile-time assertion in `main.cpp` rejects smaller MQTT configurations.
Serial `main_stack_min` reports the minimum unused main stack since task creation
in bytes. Measure after connection, command/configuration traffic and reconnect,
not just at boot. See [Espressif's stack measurement guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/performance/ram-usage.html).
The 8192-byte setting is a provisional budget pending these hardware measurements.
The receive loop reuses one message buffer for normal reads and overflow drain.
The installed ESP32-S3 debug compiler reports its frame reduced from 1776 to
912 bytes. This individual frame measurement does not bound the full call chain.

Queue memory is additional to ESP-MQTT, Wi-Fi and JSON allocations; measure
actual free heap and task stack margins on the car. Connection-loss events are
latched so a disconnect/reconnect between control ticks still disarms. Stale
state acknowledgements can be retransmitted after reconnect, so consumers must
correlate request/session IDs and consult current online state.

## Connection Configuration

Wi-Fi and MQTT connection values are configured locally with `idf.py
menuconfig`. Generated `sdkconfig` files are Git-ignored.

| Setting | Initial value | Purpose |
| --- | --- | --- |
| `CNB_ENABLE_WIFI` | `y` | Enable the existing Wi-Fi driver |
| `CNB_WIFI_SSID` | Local value | 2.4 GHz network name |
| `CNB_WIFI_PASSWORD` | Local value | Wi-Fi password |
| `CNB_ENABLE_MQTT` | `y` | Enable MQTT support |
| `CNB_MQTT_BROKER_URI` | Local value | For example `mqtt://192.168.x.x:1883` |
| `CNB_MQTT_CLIENT_ID` | `cnb-vagrant` | Unique identity for this car |
| `CNB_MQTT_USERNAME` | `cnb-vagrant` | Mosquitto account |
| `CNB_MQTT_PASSWORD` | Local value | Mosquitto password |
| `CNB_MQTT_KEEPALIVE_SEC` | `30` | MQTT connection health interval |

The broker address must be the computer's LAN address, not `localhost`.
Credentials must not be committed. They are still present in the compiled
firmware image, which is accepted for this private prototype network.

Topic names, schemas, QoS rules, retain rules, and safety limits are firmware
protocol constants. They are not per-developer `menuconfig` options.

Initial transport settings:

- MQTT 3.1.1
- Plain MQTT over TCP port 1883 on the private development network
- Username/password authentication and Mosquitto topic ACLs
- Clean MQTT session

## Topics

All topics use `cnb/vagrant` as their root.

| Topic | Direction | QoS | Retained | Purpose |
| --- | --- | ---: | --- | --- |
| `cnb/vagrant/telemetry` | Car to computer | 0 | No | Periodic state snapshot |
| `cnb/vagrant/config/set` | Computer to car | 1 | Yes | Complete desired runtime configuration |
| `cnb/vagrant/config/state` | Car to computer | 1 | Yes | Active configuration and validation result |
| `cnb/vagrant/command` | Computer to car | 1 for start/stop, 0 for heartbeat | No | Transient runtime control |
| `cnb/vagrant/command/state` | Car to computer | 1 | Yes | Last command result and current control state |
| `cnb/vagrant/status` | Car to computer | 1 | Yes | Online status and Last Will |

The application topic names are centralized in `CommunicationTopics` near the
top of `firmware/main/source/system/logic/logic.cpp`. Its `publish` section
contains topics sent by the car and its `subscribe` section contains topics
read by the car. Rename an entry there to change its path, or set an existing
entry to `nullptr` to disable that communication path.

A new topic with a new payload meaning also requires a corresponding schema,
validation and handler in `app::communication::Manager`; adding only a string
would subscribe or publish without defining safe behavior. This keeps protocol
behavior out of the vehicle logic.

## Runtime Configuration

The computer sends the three required numeric settings in one message.
`driver_style` is an optional V1 extension: omission preserves the active RAM
style, so existing clients remain compatible. The schema version remains `1`.
Older firmware rejects this new field; build and flash updated firmware first.

```json
{
  "schema_version": 1,
  "revision": 12,
  "stop_distance_cm": 35.0,
  "drive_duty": 0.4,
  "telemetry_interval_ms": 1000,
  "driver_style": "decide_action"
}
```

| Field | Default | Accepted range |
| --- | ---: | ---: |
| `stop_distance_cm` | `30.0` | `30.0` to `70.0` cm |
| `drive_duty` | `0.5` | `0.0` to `0.5` |
| `telemetry_interval_ms` | `1000` | `200` to `5000` ms |
| `driver_style` (optional) | `decide_action` | `decide_action`, `slow_left`, `slow_right`, `gradual_sweep` |

Validation is atomic. A missing required field, unknown field, invalid type, unsupported
schema, non-finite number, or out-of-range value rejects the complete update.
The previously active safe configuration remains unchanged. Before any remote
configuration applies, `config/state` reports revision `0` and result `defaults`.

The car publishes an accepted state as:

```json
{
  "schema_version": 1,
  "revision": 12,
  "result": "applied",
  "stop_distance_cm": 35.0,
  "drive_duty": 0.4,
  "telemetry_interval_ms": 1000,
  "driver_style": "decide_action"
}
```

A rejected response reports the requested revision, an error code, and the
configuration that remains active:

```json
{
  "schema_version": 1,
  "revision": 13,
  "result": "rejected",
  "error": "drive_duty_out_of_range",
  "stop_distance_cm": 35.0,
  "drive_duty": 0.4,
  "telemetry_interval_ms": 1000,
  "driver_style": "decide_action"
}
```

Supported validation errors include `invalid_json`, `missing_field`,
`unknown_field`, `duplicate_field`, `unsupported_schema`, `invalid_type`,
`invalid_revision`,
`stop_distance_out_of_range`, `drive_duty_out_of_range`,
`telemetry_interval_out_of_range`, `invalid_driver_style`,
`driver_style_requires_disarmed`, and `stale_revision`. Style strings are case
sensitive; a non-string style reports `invalid_type`.

A duplicate QoS 1 delivery with the same revision and values is idempotent. It
does not change control behavior and may cause the active state to be published
again. A lower revision, or the same revision with different values, is stale
during the current boot. After reboot, the retained broker configuration is
validated and applied as the first remote configuration.

### Driver-style selection

| PowerShell option | Wire value | Navigation | Duty cap |
| --- | --- | --- | ---: |
| `DecideAction` | `decide_action` | SCRUM-16 autonomous path selection | `0.5` |
| `SlowLeft` | `slow_left` | Left command, −90° | `0.2` |
| `SlowRight` | `slow_right` | Right command, +90° | `0.2` |
| `GradualSweep` | `gradual_sweep` | Sweep −90° to +90°, 5° per valid navigation tick | `0.2` |

The three test styles inhibit motion for an obstacle at any sensor. All modes
require valid sensors, an active start/heartbeat session and the runtime duty
limit. These are existing navigation modes, not arbitrary remote servo angles.

`Control::applyConfiguration` rejects an actual style change while armed with
`driver_style_requires_disarmed`, leaving all values and the accepted revision
unchanged. Resending the same style with valid new numeric settings is allowed
while armed. A duplicate remains idempotent. Stop and confirm disarmed before
switching modes, check `config/state` for `applied` and the expected style, then
start a new session. Stopping alone does not retry a rejected configuration.

The active style lives in `runtime::Configuration`. `Logic::decideAction`
synchronizes it to `Planner` only when it changes, preserving sweep progress
between ticks. The local `Logic::setDriverStyle` returns success/failure and uses
the same disarmed guard through `Control::setDriverStyle`; it does not allocate
a MQTT revision. Mode changes do not command the servo while disarmed.

`config/state` always reports the active `driver_style`, including on rejection.
`command/state` and telemetry also report it. The latter's `steering_deg` remains
the last accepted servo command, not proof of physical position.

Retained storage is per message: a later config without `driver_style` replaces
the broker's entire retained config. It preserves the current RAM style but
restores the compiled `decide_action` default after reboot unless the new retained
config explicitly supplies another mode. Include the style on every publication
when its restoration is desired. A rejected retained config also remains at the
broker and may apply when redelivered after a disconnect has disarmed the car.
Replace an unwanted retained request with the desired configuration and a new
revision; neither acceptance nor reconnection starts the car.

## Start, Stop, and Heartbeat

Commands are transient and must never be retained. `run-car.ps1` creates an
opaque session ID for each operator run.

Start command, QoS 1:

```json
{
  "schema_version": 1,
  "request_id": 21,
  "session_id": "8f29c1",
  "command": "start"
}
```

Heartbeat, once per second with QoS 0:

```json
{
  "schema_version": 1,
  "session_id": "8f29c1",
  "command": "heartbeat"
}
```

Stop command, QoS 1:

```json
{
  "schema_version": 1,
  "request_id": 22,
  "session_id": "8f29c1",
  "command": "stop"
}
```

`start` selects the active session and arms autonomous driving. Only heartbeat
messages from that session refresh the three-second control lease. Heartbeat
never arms the car. A valid `stop` always disarms the car, even when its session
ID does not match. Duplicate start and stop deliveries are idempotent.

Start/stop IDs are positive 32-bit integers. A fresh start must have an ID above
the highest start/stop ID observed during this boot. A duplicate start is
acknowledged only while that exact session remains armed with a valid lease;
it never refreshes the lease. After stop, disconnect or timeout the old start
is rejected as `stale_request`. An expired heartbeat cannot renew the session,
even if it is processed before the next safety evaluation. Stop is always
honored regardless of whether its ID is older.

The operator scripts share a locked, Git-ignored `.revision` file and choose
`max(current Unix seconds, stored ID + 1)`. Use one operator checkout per car;
independent computers must coordinate their counters. IDs and the car's
high-water mark are not authentication and reset on firmware reboot.

The car publishes command state when a command is handled or control state
changes:

```json
{
  "schema_version": 1,
  "last_request_id": 21,
  "session_id": "8f29c1",
  "result": "accepted",
  "driver_style": "decide_action",
  "control_state": "armed",
  "motion_state": "inhibited",
  "reason": "obstacle"
}
```

`control_state` is `disarmed` or `armed`. `motion_state` is `stopped`, `moving`,
or `inhibited`. Reason values are `boot`, `operator_stop`, `obstacle`,
`sensor_fault`, `heartbeat_timeout`, `mqtt_disconnected`, `message_overflow`,
`actuator_fault`, and `none`. Command rejection errors also include `invalid_session`,
`unsupported_command`, `not_armed`, `session_mismatch`, and
`retained_command`, `invalid_request`, and `stale_request`.
`session_id` is empty when disarmed. Reports without a parsed request ID use
`last_request_id: 0`; they do not borrow the previous command's ID.

The 20 Hz control decision is equivalent to:

```text
if not armed:
    motor output = 0
else if MQTT is disconnected or heartbeat lease expired:
    disarm; motor output = 0
else if any required sensor value is invalid:
    inhibit motion; motor output = 0
else if obstacle in selected path is inside stop_distance_cm:
    inhibit motion; motor output = 0
else:
    steer toward selected path; use min(drive_duty, navigation duty)
```

Normal navigation uses duty `0.5`, test styles `0.2`. Forward wins only when
strictly farther than both sides; otherwise left wins if farther than right,
and right wins ties. Left steering is negative, forward is zero, right positive.
For example left/forward/right `10/20/70` selects right and can move; an invalid
left reading still inhibits motion. These are the merged SCRUM-16 decisions.

An armed obstacle/sensor inhibition requests active braking (both motor PWM
outputs `1.0`, speed command `0`). Disarming lowers nSLEEP and requests both PWM
outputs `0.0`. The output guard checks motor/servo results and PWM driver state
because MP6550 currently hides some PWM failures. These checks do not measure
electrical outputs or guarantee mechanical braking. The existing MP6550 driver
is unchanged.

## Telemetry

Telemetry is a periodic coherent snapshot, not a command or persistence format.

```json
{
  "schema_version": 1,
  "sequence": 184,
  "uptime_ms": 15230,
  "distance_cm": {
    "left": 42.3,
    "center": 55.1,
    "right": 37.8
  },
  "closest": {
    "sensor": "right",
    "distance_cm": 37.8
  },
  "adc_raw": { "left": 750, "center": 600, "right": 820 },
  "steering_deg": 0.0,
  "motor": {
    "speed_command": 0.4,
    "forward_duty": 0.4,
    "backward_duty": 0.0
  },
  "driver_style": "decide_action",
  "control_state": "armed",
  "motion_state": "moving",
  "reason": "none"
}
```

`adc_raw` contains the 12-bit raw counts (0–4095) for `left`, `center` and
`right`, captured from the same conversion used for each distance. The ADC
driver caches each read attempt; `lastRaw()` inspects it without sampling again.
Unavailable or out-of-range raw counts serialize as `null`. A valid zero raw
count is preserved even when distance is invalid. Calibration failure can leave
a valid raw count with an invalid distance. Example raw counts above are
illustrative. ADC raw counts are not calibrated volts or distance averages.
This adds a telemetry field under schema version 1; clients should tolerate
additional telemetry fields. The MQTT payload buffer remains 768 bytes; the
maximum-length telemetry test includes ADC fields and full-precision floats.

An unavailable sensor value is JSON `null`, never the invalid JSON values `NaN`
or `Infinity`. If no sensor is valid, `closest` is `null`.

`speed_command` is a normalized requested output, not measured vehicle speed in
metres per second. Physical speed requires future wheel feedback. `steering_deg`
is the last accepted servo command, not a position measurement. PWM duty fields
are driver state; nSLEEP can disable the bridge even if a failed PWM write leaves
an old duty value reported.

## Online Status

After connecting, the car publishes this retained message:

```json
{"schema_version":1,"online":true}
```

The MQTT Last Will is retained on the same topic:

```json
{"schema_version":1,"online":false}
```

Orderly shutdown attempts to publish retained `online:false` with QoS 0 before
stopping the client, after motor outputs are disabled. Delivery is best effort;
the QoS 1 Last Will covers unexpected disconnects and is subject to broker
keepalive timing.

On reconnect, the car remains disarmed and publishes fresh command state. A
consumer must treat `online:false` as authoritative even if an older retained
command state still says `armed`.

## Reconnection

The Wi-Fi and MQTT drivers expose asynchronous initialized/connected state to
the vehicle loop; their ESP-IDF event callbacks never wait for a connection.

The first connection attempt is immediate. Retry delays are `1`, `2`, `5`,
`10`, and then `30` seconds. MQTT starts
only after Wi-Fi has acquired an IP address. Wi-Fi loss stops MQTT activity.
MQTT-only failure leaves Wi-Fi active while MQTT reconnects. Successful MQTT
reconnection restores subscriptions and retained configuration but never the
armed state.

## Persistence and Logging

- Active runtime configuration exists only in ESP32 RAM.
- Armed state and command sessions exist only in ESP32 RAM.
- Firmware contains safe compiled defaults.
- Mosquitto holds the latest retained configuration and state.
- The ESP32 does not persist MQTT configuration or telemetry in NVS/flash.
- `log-telemetry.ps1` writes timestamped NDJSON on the computer. SQLite or CSV
  conversion is a separate future feature.

The ESP-IDF Wi-Fi driver may initialize NVS for its own internal operation. That
is separate from application-level MQTT persistence.

## Mosquitto Setup on Windows

Install Mosquitto so `mosquitto`, `mosquitto_pub`, `mosquitto_sub`, and
`mosquitto_passwd` are available. Then create a private directory such as
`C:\Users\<user>\cnb-mqtt` with a `data` subdirectory.

1. Copy `tools/mqtt/mosquitto.conf.example` to the private directory as
   `mosquitto.conf`, replace `REPLACE_ME`, and verify all absolute paths.
2. Copy `tools/mqtt/mosquitto-acl.example` there as `acl`.
3. Create the password file and its two users:

```powershell
mosquitto_passwd -c C:\Users\<user>\cnb-mqtt\passwords cnb-vagrant
mosquitto_passwd C:\Users\<user>\cnb-mqtt\passwords cnb-dashboard
```

Use different passwords. Configure the `cnb-vagrant` credentials in ESP-IDF
`menuconfig`; put only the `cnb-dashboard` credentials in the local tool
`.env`. Start the broker in a terminal:

```powershell
mosquitto -c C:\Users\<user>\cnb-mqtt\mosquitto.conf -v
```

Allow inbound TCP port 1883 only on the private Windows network. Plain MQTT
does not encrypt credentials or messages, so this setup must not be exposed to
the internet or an untrusted network.

## Operator Tools

Tools under `tools/mqtt/`:

- `.env.example`: committed configuration template without secrets
- `.env`: local Git-ignored broker address and credentials
- `set-config.ps1`: publish three required numeric settings and optional
  `-DriverStyle DecideAction|SlowLeft|SlowRight|GradualSweep`
- `watch-telemetry.ps1`: readable telemetry columns and state summaries; `-Raw`
  preserves the original topic/JSON line
- `format-telemetry.ps1`: presentation functions, including missing-field handling
- `log-telemetry.ps1`: save timestamped telemetry as Git-ignored NDJSON
- `run-car.ps1`: start a session and maintain its heartbeat until stopped
- `stop-car.ps1`: send an immediate stop command

The terminal view rounds distances and duty to two decimals and steering to
one; raw ADC values remain integers. Missing/invalid data displays `--`. The
timestamp is local receive time. MQTT and NDJSON retain original precision.
Older firmware without ADC data remains displayable. Flash the ADC firmware
once to obtain real raw counts; restarting the script updates presentation.

The scripts hide JSON, topic, revision, and authentication details during normal
operation. `run-car.ps1` opens a non-retained state subscription, publishes start
and waits up to 1.5 seconds after publication for a matching accepted request
and armed session. It retries the same start once if needed; this does not
extend the car's lease. A broker PUBACK alone does not confirm vehicle acceptance.
Missing/rejected acknowledgement sends a best-effort stop and exits without
heartbeat. After acceptance it sends heartbeat and monitors session state.
Stopping the script normally sends `stop`; loss of the script or network is
covered by the heartbeat timeout. Each publication is limited to two seconds.
The scripts support Windows PowerShell 5.1 and pass JSON via UTF-8 stdin to avoid
native argument quoting changing the payload.

Create the local settings once:

```powershell
Copy-Item tools/mqtt/.env.example tools/mqtt/.env
```

Edit `.env`, then use these commands from the repository root:

```powershell
.\tools\mqtt\watch-telemetry.ps1
.\tools\mqtt\log-telemetry.ps1
.\tools\mqtt\set-config.ps1 -StopDistanceCm 35 -DriveDuty 0.4 -TelemetryIntervalMs 1000
.\tools\mqtt\run-car.ps1
.\tools\mqtt\stop-car.ps1
```

Keep the `run-car.ps1` terminal open while the car is allowed to drive. Each
heartbeat currently starts the small `mosquitto_pub` command-line client; this
is intentionally simple tooling for development, not a high-rate controller.
The password is passed to Mosquitto's CLI and can be visible to local process
inspection while a command runs.

To change mode, stop and confirm `disarmed` in a fresh car report, then run:

```powershell
.\tools\mqtt\set-config.ps1 -StopDistanceCm 35 -DriveDuty 0 -TelemetryIntervalMs 1000 -DriverStyle SlowLeft
```

Expected: the matching `config/state` revision reports `applied`, `slow_left`
and zero duty. Configuration does not start the car. Follow the Swedish guide's
section 8.4 for starting a test and returning to `DecideAction`.

## Verification

The implementation is not complete until the following evidence exists:

1. Host tests cover valid and invalid configuration, duplicate messages,
   command sessions, heartbeat timeout, state transitions, SCRUM-16 navigation,
   output failures, strict JSON, telemetry and reconnection. Passed on 2026-09-08
   with Zig 0.14.1 (C++17, warnings as errors) and cJSON 1.7.19, including style
   validation, atomic armed rejection, legacy omission and telemetry buffer size.
   Isolated Mosquitto 2.1.2 tests cover accepted/missing/rejected car ACKs,
   monotonic counters, all four script style mappings, retained payloads and
   omission. These tests use a simulated car.
2. A complete ESP32-S3 build, link and partition-size check are required for the
   stack-overflow fix. A user flash of the driver-style extension revealed a
   main-task stack overflow; compiler success did not establish stack safety.
   The user built/flashed the preceding MQTT version on
   2026-09-08; shared logs confirm Wi-Fi/MQTT, zero motor duty and disarmed
   telemetry. This is not build or hardware evidence for the new extension.
   ESP-IDF Python remains blocked by Windows in the agent environment.
3. Hardware monitoring confirms the 20 Hz loop remains responsive while the
   broker is unavailable and while reconnecting.
4. The car boots stopped, requires a fresh start, and stops on command.
5. The car stops within three seconds after heartbeat or MQTT loss.
6. Invalid sensor data inhibits motion; obstacles inhibit the selected path
   according to the normal/test-style rules above. Verify active braking and
   nSLEEP shutdown physically, including servo direction and PWM transitions.
7. Retained configuration is restored after reconnect, but retained data never
   starts the car.
8. Telemetry fields, rates, and invalid sensor representation match this
   document.
9. On hardware, style changes are rejected atomically while armed (including
   zero duty), accepted after stop, and reflected in status/telemetry. Verify
   each mode's servo behavior and stopping rule and retained restoration.
10. Credentials and generated runtime data are absent from Git.

Direct servo angle commands and calibration settings are intentionally excluded until physical center
and safe left/right limits have been calibrated.


### Running host tests

From the repository root, with CMake and a host C++ compiler installed:

```text
cmake -S firmware/test -B firmware/test/build -DCJSON_DIR=<absolute-path-to-cJSON>
```

Expected: configuration succeeds. Use cJSON 1.7.19, or omit `CJSON_DIR` after
ESP-IDF has resolved the managed cJSON component.

```text
cmake --build firmware/test/build
```

Expected: `unit_tests` builds.

```text
ctest --test-dir firmware/test/build -C Debug --output-on-failure
```

Expected: all tests pass. Host tests never connect to the car. The test-only
`sdkconfig.h` enables injected network drivers; `FreeRTOSConfig.h` is an empty
shim for the motor header's unused include and is not used by firmware builds.

```text
python tools/mqtt/tests/test_operator_tools.py
```

Expected: acknowledgement scenarios, revision checks, driver-style mappings,
retained config and legacy omission pass. This Windows test starts an isolated broker on a temporary loopback
port and uses dummy credentials; it does not read the operator `.env` or contact
the car. Temporary test logs are kept at the path printed by the test.

### Testing terminal presentation

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/mqtt/tests/test_telemetry_display.ps1
```

Expected: readable values, ADC zero/null, older payloads, Swedish locale,
configuration errors, session IDs and unchanged raw JSON all pass. This test
does not connect to a broker. Host C++ tests also verify that cached ADC values
belong to the distance sample and that valid zero counts survive serialization.
