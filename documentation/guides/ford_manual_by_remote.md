# Ford: drive from the web page (ManualByRemote)

The Ford has two drive styles. This guide covers **ManualByRemote**: you set the
steering command (−90 left … +90 right) and the speed command (−100 reverse …
+100 forward) with two sliders in the web page, and the car follows them over MQTT.

The other is **GapCalibration**, which drives a fixed script to measure the
wheel's magnet gaps instead of following you. Pick a style in the DRIVE STYLE
panel while the car is stopped, then Start to run it. The car boots on
ManualByRemote and does not remember a choice, so a power cycle brings it back.
See [the calibration section](#8-measure-the-magnet-gaps-gapcalibration).

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
Then build and flash. Unlike Vagrant, the Ford ignores the Wi-Fi and broker
settings in menuconfig: it joins `cnb-net` and the broker at `192.168.137.1`,
both compiled into `fordLogic.cpp`.

### Join another Wi-Fi network over serial

To use another network without rebuilding, open `idf.py monitor` and type:

```text
wifi ssid <network name>
wifi pass <password>
wifi save
```

Then press reset. The network is stored in NVS and survives reflashing. Leave out
the password (`wifi pass` alone) for an open network. `wifi` shows the network in
use, and `wifi clear` goes back to `cnb-net`. The broker address does not change,
so the new network must still reach a broker at `192.168.137.1`.

The serial commands only change network settings, never the motor, and they work
whether the car is armed or not. The network only changes at the next restart.

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

## Motor temperature

The **MOTOR TEMP** tile shows the TMP36 on the motor can (A0), averaged over
about 1.6 s. It turns amber at 40 °C (WARM · ease off) and red at 45 °C
(HOT · stop and let it cool). The car does not stop by itself: the operator
does. "No temperature reading" means the sensor is missing or reads outside
−40…125 °C, which is usually a loose wire.

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

## 8. Measure the magnet gaps (GapCalibration)

The odometer's speed is only right if the firmware knows how far apart the
magnets are. Ford's six are deliberately uneven, so this is measured rather than
assumed. Until it has been, the car uses the design values.

This used to need the I2C wiring, the `A89301_CONFIG_MODE` build and the `cal`
command. It does not any more: it runs on the PWM wiring the car already drives
on. The `cal` command still exists for when the car is already wired for I2C.

1. **Lift the car** so the right rear wheel turns freely. Arming is the only
   confirmation, so nothing else stops it driving away.
2. Stop the car if it is armed, then choose **GapCalibration** in the DRIVE STYLE
   panel. A style can only be chosen while the car is stopped.
3. Press **Start**. The wheel spins at three speeds for about a minute. The
   MAGNET GAP CALIBRATION panel shows which speed it is on and how many
   revolutions it has averaged.
4. The car stops itself and disarms when it finishes. That is not a fault.
5. If it says MEASURED, check the spread, then press **Confirm & store**. Nothing
   is written to flash until you do.
6. Restart the car. The stored table is read at start-up.

The panel reports the whole reason when a run produces no table, and each one is
an instruction rather than an error code:

| It says | What to do |
| --- | --- |
| A gap changed with speed | The magnets are too even, or the motor's ripple is in the reading. Space them more unevenly and measure again. |
| The wheel stopped turning | Battery on? Wheel free? |
| The motor can got too hot | Let it cool, then measure again. |
| Too evenly spaced | The gaps cannot be told apart at all. Space them more unevenly. |
| Stopped before it finished | Nothing is stored. A part-measured wheel is not a calibration. |

A run that is stopped, stalls or disagrees stores nothing, and starting a new run
clears whatever was waiting to be confirmed. There is no way to store a table the
car refused: a gap that changes with speed is the motor and not the wheel, and a
table of motor cogging stored as wheel geometry is worse than no table at all.

> Without a motor temperature sensor taped to the can, the run goes ahead with the
> overheat guard off and the panel says so. The duties are low and the run is
> bounded, but its safe duration has never been measured on a bare motor.
