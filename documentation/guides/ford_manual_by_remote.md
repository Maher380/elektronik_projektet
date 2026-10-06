# Ford: drive from the web page (ManualByRemote)

The Ford has three drive styles. This guide covers **ManualByRemote**: you set the
steering command (−90 left … +90 right) and the speed command (−100 reverse …
+100 forward) with two sliders in the web page, and the car follows them over MQTT.

The other two drive a fixed script instead of following you: **GapCalibration**
measures the wheel's magnet gaps, and **SpeedCalibration** learns the duty for
each speed and how far the car needs to stop. Pick a style in the DRIVE STYLE
panel while the car is stopped, then Start to run it. The car boots on
ManualByRemote and does not remember a choice, so a power cycle brings it back.
See [the gap calibration](#8-measure-the-magnet-gaps-gapcalibration) and
[the speed calibration](#9-calibrate-speed-on-the-floor-speedcalibration).

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

Speed command ±1 … ±100 maps to duty 0.08 … 0.30. On the floor with the Pi and
its power bank aboard, the car needs about duty 0.12 (command 19) to start, and
duty 0.20 (command 55) gave 1.57 m/s. Nothing above 0.20 has been measured yet. These values are compiled into `fordLogic.cpp` until they move to NVS.

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

> **Skip this on Ford's current wheel.** Its two magnets are evenly spaced, so the
> odometer is exact without a gap table and this run always fails, with "A gap
> changed with speed" or "Too evenly spaced". See
> [ford_odometer.md](../design_documents/ford_odometer.md). The steps below are for a
> wheel with unevenly spaced magnets.

The odometer's per-gap speed is only right if the firmware knows how far apart
unevenly spaced magnets are, so this is measured rather than assumed. Until it has
been, the car uses the design values.

Every run also logs each revolution on the serial monitor, on lines tagged `CAL`.
When a run fails, those lines show what the sensor actually saw.

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

## 9. Calibrate speed on the floor (SpeedCalibration)

This learns what Ford needs to drive at a given speed, and how far it needs to
stop, with the car **on the floor** and the odometer as the reference. The
results are the starting point for a speed loop that drives in m/s instead of
duty.

Every leg is 5 m from start to standstill, and the legs alternate forward and
back, so the car shuttles between the same two marks. A run has twelve legs:

| Legs | What it does |
| --- | --- |
| 1–8 | 0.8, 1.0, 1.2 and 1.4 m/s from standstill, each forward then back |
| 9–10 | A speed step: 0.8 → 1.4 m/s forward, 1.4 → 0.8 m/s back |
| 11–12 | Lowest speed: start at duty 0.12, then step the duty down once a second until the wheel stalls |

On the speed legs the car holds the target with a feed-forward duty plus a small
PI loop on the odometer speed. It brakes when the distance left equals its
predicted stopping distance, k · v², so it stops on the mark. After every leg it
learns: the duty that held a speed becomes that speed's feed-forward, and k is
updated from the stopping distance it actually needed. The first legs therefore
stop further from the mark than the later ones. What it has learned is kept
until the car restarts, so a second run starts where the first left off.

The lowest-speed legs end where the wheel stalls, not on the mark. They are the
last two, and the back leg stalls about as far out as the forward one.

1. Mark a start point with 5 m of straight, clear floor ahead and about 0.5 m
   spare at each end. Put the car on the mark, facing along the run.
2. Stop the car if it is armed, then choose **SpeedCalibration** in the DRIVE
   STYLE panel.
3. Press **Start**. Stay ready on **PANIC STOP**: the steering does not correct,
   so nothing but you stops the car if it drifts.
4. The SPEED CALIBRATION panel adds a row per leg as it finishes. The car stops
   itself and disarms when it is done. That is not a fault.
5. The console server collects the legs, so the page can be closed and opened
   again. It also writes each run to
   `tools/mqtt/logs/speed_calibration-<date and time>.csv`; the panel names the
   file. The table is also there as CSV to copy.

| Column | Meaning |
| --- | --- |
| SPEED, DUTY | Steady speed and the mean duty that held it. On a lowest-speed leg, the lowest duty that kept the car rolling and its speed. |
| RISE | Time from the wheel turning (or the speed step) to within 5 % of the target. |
| OVERSHOOT | How far past the target the speed went, in m/s. |
| STOP | Braking distance. |
| OFF MARK | How far from the 5 m mark the car stopped. |

| Result | Meaning |
| --- | --- |
| reached | The target was held and measured. |
| held briefly | Reached, but too briefly to measure a steady speed. |
| not reached | The car had to brake before it reached the target. SPEED is its speed at braking. |
| stalled below | Lowest-speed leg: the wheel stalled at the next step down. |
| rolled at every step | Lowest-speed leg: even the lowest step kept it rolling. |
| no start / stalled | The wheel did not turn, or stopped before anything was measured. |

The recipe and the loop gains are `app::ford::speed_calibration` in
`system/ford.h`. Its starting values come from the first floor run: no start at
duty 0.08, a stall at 0.10, 0.82 m/s at 0.12, and nothing steady below about
0.8 m/s.
