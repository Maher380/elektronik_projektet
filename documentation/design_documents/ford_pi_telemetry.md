# Ford: Raspberry Pi telemetry over MQTT

The contract between the Ford's Raspberry Pi (code in the `slam_test` repo) and the
web page in this repo (`tools/mqtt-ui`). The Pi reports what it **measures** with the
camera through SLAM, so the operator can compare it with what the car was **asked**
to do. Terms are defined in [CONTEXT.md](../../CONTEXT.md): *measured speed*,
*wheel angle*, *steering command*, *speed command*.

## Topics

| Topic | Retained | QoS | Sent |
| --- | --- | --- | --- |
| `cnb/ford/pi/telemetry` | no | 0 | every 200 ms |
| `cnb/ford/pi/status` | yes | 1 | on connect, and as the MQTT last will |

The Pi is part of the Ford, so its topics sit under `cnb/ford`. It has its own
status, so the page can tell "Pi offline" from "ESP offline".

## Broker login

- Username and password: the same as the Ford's ESP, `cnb-ford`.
- **Client ID: `cnb-ford-pi`.** It must differ from the ESP's (`cnb-ford`); two
  clients with the same ID keep disconnecting each other.
- The ACL ([mosquitto-acl.example](../../tools/mqtt/mosquitto-acl.example)) lets
  `cnb-ford` write both Pi topics and `cnb-dashboard` read them.
- The broker is on the car network, `mqtt://192.168.137.1:1883`.

## `cnb/ford/pi/status`

Exactly like the ESP's status:

```json
{ "schema_version": 1, "online": true }
```

Publish it retained when connected, and register
`{ "schema_version": 1, "online": false }` (retained) as the last will so the broker
reports the Pi offline if it drops off.

## `cnb/ford/pi/telemetry`

```json
{
  "schema_version": 1,
  "measured_speed_mps": { "slam": 0.42 },
  "wheel_angle_deg": { "slam": -12.3 },
  "slam_state": "tracking",
  "cpu_temp_c": 61.2
}
```

| Field | Unit | Meaning |
| --- | --- | --- |
| `schema_version` | — | Always `1`. Other versions are ignored. |
| `measured_speed_mps` | m/s | Measured speed, one entry per source. Positive is forward. |
| `wheel_angle_deg` | degrees | Wheel angle, one entry per source. Negative is left, 0 straight ahead, positive right. |
| `slam_state` | — | `"tracking"`, `"lost"` or `"starting"`. Anything else makes the page ignore the message. |
| `cpu_temp_c` | °C | The Pi's CPU temperature. |

### Sources

A real-world value is always reported with its source. The key names the source,
the value is a number or `null`:

- `slam`: worked out by SLAM from the camera. The only source so far.
- Later sources get their own key, for example
  `"measured_speed_mps": { "slam": 0.42, "odometer_left_rear": 0.45 }`.
  Keys use `a-z`, `0-9` and `_`, at most 32 characters.

### When a value is `null`

- `slam_state` is not `"tracking"`: both SLAM values are `null`.
- The car moves slower than **0.2 m/s**: the SLAM wheel angle is `null`. It is worked
  out from turn rate and speed, so it is unknown when the car stands still or creeps.
  Tune the threshold in `slam_test`.
- Any value the Pi cannot work out: `null`, never a made-up 0.

## What the page does with it

- **Status strip, PI · SLAM:** SLAM state, or STALE/OFFLINE; CPU temperature amber
  from 70 °C, red from 80 °C ("throttling, SLAM may lag"; the Pi 5 slows itself down
  at about 80–85 °C).
- **Steering card:** the SLAM wheel angle beside the steering command converted to
  degrees with the Ford's full-lock angle (`FULL_LOCK_DEG` in `public/ford.mjs`,
  a placeholder of 25° until measured). A difference above 5° is flagged.
- **Speed card:** the SLAM measured speed beside the speed command, not converted:
  without a speed loop a speed command has no fixed m/s.
- **Charts:** the last 30 seconds of command and measured, for steering and speed.
- **Freshness:** the page assumes the fixed 200 ms rate and treats Pi data older than
  500 ms as stale. If the Pi's rate changes, change `PI_INTERVAL_MS` in `core.mjs`
  and `public/ford.mjs` too.
- **Demo:** `start-ui.ps1 -Car ford -Demo` simulates the Pi with this payload.

## Checking from the laptop

```powershell
& 'C:\Program Files\mosquitto\mosquitto_sub.exe' -h 127.0.0.1 -u cnb-dashboard -P <password> -t 'cnb/ford/pi/#' -v
```
