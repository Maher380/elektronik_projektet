# Ford: drive from the web page (ManualByRemote)

The Ford has one drive style, **ManualByRemote**: you set the steering command
(−90 left … +90 right) and the speed command (−100 reverse … +100 forward) with
two sliders in the web page, and the car follows them over MQTT.

Broker, `.env` and Wi-Fi are set up as in the [MQTT guide](mqtt_steg_for_steg.md).
This guide only lists what is different for the Ford.

## 1. Before the first drive

- Wire the A89301 for **PWM speed** and fit the pull resistors, see
  [the motor controller doc](../design_documents/ford_a89301_motor_controller.md#safety).
- Broker: copy the updated `tools/mqtt/mosquitto-acl.example` to
  `%USERPROFILE%\cnb-mqtt\acl` and add the car's account, then restart Mosquitto:

  ```powershell
  & 'C:\Program Files\mosquitto\mosquitto_passwd.exe' "$env:USERPROFILE\cnb-mqtt\passwords" cnb-ford
  ```

## 2. Build and flash

In `idf.py menuconfig`, choose **Target car → Ford**. The MQTT client ID and
username become `cnb-ford`; a Ford build with another client ID does not compile.
Set Wi-Fi, broker URI and the `cnb-ford` password as for Vagrant, then build and
flash.

## 3. Start the page

```powershell
.\tools\mqtt-ui\start-ui.ps1 -Car ford
```

Open **http://127.0.0.1:8765**. Without a car: add `-Demo`.

### Drive from a phone on cnb-net

The page is only reachable from the laptop until you turn on LAN access:

1. On the laptop, open **http://127.0.0.1:8765** and click the **LAN OFF** chip
   at the top. It turns into **LAN ON · 192.168.137.1:8765** (your address may differ).
   The chip is only shown, and only works, on 127.0.0.1.
2. The first time, Windows asks whether Node.js may accept connections. Allow it
   for the network type cnb-net uses (the hotspot is often *Public*).
3. On the phone (connected to cnb-net), open the address the chip shows.

Anyone on the network can then open the page and drive the car. Click the chip
again to turn LAN access off; phones lose the page at once. LAN access is always
off when the console starts.

## Drive with the arrow keys

Instead of dragging the sliders you can use the **arrow keys** or the
▲ ◀ ▼ ▶ pad on the page (works on a touch screen too):

| Press | Does |
| --- | --- |
| ↑ / ↓ | Speed +1 / −1 |
| ← / → | Steering −10° (left) / +10° (right) |
| 0 | Speed 0 (steering stays) |

Holding a key repeats at the keyboard's repeat rate. The value stays where it is
when you let go; press the **0** key, the 0 button, double-click the slider or
panic stop to stop.

## What the Raspberry Pi measures

When the Pi publishes (see [the Pi telemetry contract](../design_documents/ford_pi_telemetry.md)),
the page shows what the camera measured beside what the car was asked to do:

| Where | Shows |
| --- | --- |
| Status strip, **PI · SLAM** | TRACKING / TRACKING LOST / STARTING, or STALE / OFFLINE; CPU temperature (amber ≥ 70 °C, red ≥ 80 °C) |
| Steering card, **WHEEL · SLAM** | Wheel angle in degrees; orange when more than 5° from the command. "—" below 0.2 m/s |
| Speed card, **MEASURED · SLAM** | Measured speed in m/s |
| Charts | Last 30 s of command and measured, for steering and speed |

The command in degrees uses the Ford's full-lock angle, a placeholder of 25° until
it has been measured (`FULL_LOCK_DEG` in `tools/mqtt-ui/public/ford.mjs`).

## How the car behaves

| Situation | Car |
| --- | --- |
| Boot, disarmed, panic stop, heartbeat lost (3 s), MQTT lost | Brakes and centres the steering |
| Speed command 0 | No drive: motor coasts, still armed |
| Speed command changes direction | Brakes 300 ms, then drives the other way |
| No drive command for 500 ms | No drive, still armed; drives on when commands return |
| Start | Speed stays 0 until you move the speed slider |

Speed command ±1 … ±100 maps to duty 0.08 … 0.15 (about 0.8 … 1.5 m/s
unloaded). These values are compiled into `fordLogic.cpp` until they move to NVS.

## Bench checklist

Run it with the car on the stand, wheels in the air, before driving on the floor.

- [ ] Flash: the wheels do not move while flashing or booting.
- [ ] Page shows BROKER CONNECTED, CAR RECEIVING and drive style ManualByRemote.
- [ ] Start: motor state goes from BRAKED to NO DRIVE; moving the speed slider is
      needed before the wheels turn.
- [ ] Speed +20 / +100: wheels turn forward; the CAR value matches the slider.
- [ ] Speed −40 from forward: motor state shows BRAKING briefly, then DRIVING REV.
- [ ] Steering −90 / 0 / +90: wheels follow; the CAR value matches the slider.
- [ ] Press ↑ / ↓: the speed slider and its CAR value change by 1 per press.
- [ ] Press ← / →: the steering slider and its CAR value change by 10 per press.
- [ ] Drive at speed +20 and press the 0 key: speed slider and CAR value go to 0.
- [ ] Pull the laptop's Wi-Fi: DRIVE TIMEOUT (wheels coast) within about 0.5 s,
      then disarmed (brake) within about 3 s.
- [ ] Switch to another tab while driving: the car brakes and disarms.
- [ ] Panic stop from a second tab: the car brakes and disarms.

> @todo No automated tests yet for the ManualByRemote rules (firmware) or the
> drive stream (console). Add them before relying on this beyond bench tests.
