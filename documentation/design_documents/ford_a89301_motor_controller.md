# Ford: A89301 BLDC Motor Controller

## Human introduction
Initially the A89301 had bad settings for the ford Engine. In order to fix this a configuration application which communicated with the A89301 from the esp32s3 through I2C was developed. This tool can also be used to tune the A89301 later on.

## AI text

Bench bring-up and tuning of the drive motor for **ford**, done on branch
`feature/add_A89301_motor_driver` (September 2026). The car was on a stand,
wheels in the air, powered from a lab supply at 7.4 V. All numbers below are
**without load** unless stated otherwise.

## Hardware

| Part | Details |
| --- | --- |
| Motor controller | Pololu A89301-based sensorless brushless motor controller, 50 V, 11 A (Pololu 5357, board `md47a`) |
| Motor | Sensorless brushless, 8000 KV, 3 wires, about 0.5 Ω phase to phase (measured) |
| Pole pairs | 1 (derived: 12 Hz electrical per wheel rev/s matches 8000 KV at 1 pole pair and a gear ratio of about 12) |
| Wheel | 34 mm diameter, 0.107 m circumference |
| Controller MCU | Arduino Nano ESP32 (ESP32-S3) |

The A89301 is a field-oriented (FOC) sensorless controller. It estimates the
rotor position from the motor model (resistance, inductance) and an observer
with gains `PID_P` and `PID_I`. These parameters must fit the motor.

## Wiring

### Configuration wiring (I2C), used by `A89301_CONFIG_MODE`

| A89301 board | ESP32-S3 (Nano pin / GPIO) | Notes |
| --- | --- | --- |
| VIN, GND (screw terminal) | – | 7.4 V supply (2S LiPo range 6.4–8.4 V) |
| SA, SB, SC | – | Motor phases |
| GND (header) | GND | Common ground is required |
| IOREF | 3V3 | Logic level 3.3 V. Also powers the FAULT LED |
| SPD/SCL | A6 / GPIO13 | I2C SCL. **Add 4.7 kΩ pull-up to 3.3 V** (the board has none on SCL) |
| FG/SDA | A5 / GPIO12 | I2C SDA. The board has a pull-up to IOREF |
| DIR | D4 / GPIO7 | High = forward in the test apps |
| BRAKE | D2 / GPIO5 | High = brake. **12 kΩ pull-up to 2V8 (R1)**, see Safety |
| FLT | D3 / GPIO6 | Open drain, pulled up to IOREF; low = fault. It flashes while the fault lasts, seen when the motor stalls. Wired 2026-10-07; reported as `motor_fault` in telemetry, not acted on |
| – | D9 / GPIO18 | A3144 wheel odometer, 2 magnets on the right rear wheel |
| – | A0 / GPIO1 | TMP36 on the motor can (ADC1) |

### PWM wiring, used by the Ford car app and `MOTOR_TEST_MODE`

Same as above, but SPD is driven with 20 kHz PWM from **D5 / GPIO8** instead of
the I2C bus: **move the SPD/SCL wire from A6 to D5**. The A89301 EEPROM is
configured for PWM speed input (`SPD_MODE` 0, `CLOCK_PWM` 0, `PWMIN_RANGE` 0 =
PWM above 2.8 kHz).

### Switching between the two wirings

The A89301 has one pin for both jobs: SPD (PWM speed input) and SCL (I2C clock).
SCL needs a pull-up and SPD needs a pull-down (R2, see Safety), and both cannot
sit on the same wire: together they hold a floating SPD at about 2.2 V, which is
full speed. So the wire moves, and each resistor stays at its own Nano pin:

- the 4.7 kΩ pull-up from **A6** to 3V3,
- R2 (4.7 kΩ) from **D5** to GND.

Only the SPD/SCL wire moves. DIR, BRAKE, FG/SDA (A5), the odometer (D9), the
TMP36 (A0) and the battery divider (A3) stay where they are in both wirings.

**Car → config app** (for example `cal`, the magnet gap calibration):

1. Battery off.
2. Move the SPD/SCL wire from **D5** to **A6**.
3. In `firmware/main/source/main.cpp`, uncomment `#define A89301_CONFIG_MODE`.
   Build and flash.
4. Start the ESP32, then switch the battery on.

**Config app → car:**

1. Type `stop`, then switch the battery off. The battery must go off: the I2C
   speed mode bit (`spd`, `cal`, `sweep`, `run`, `profile`) lives in a working
   register, and only a power cycle of the A89301 clears it. An ESP32 reset or
   reflash does not. If it is still set, the chip ignores the PWM on SPD and the
   car does not respond to speed commands.
2. Move the SPD/SCL wire from **A6** back to **D5**.
3. Comment `A89301_CONFIG_MODE` out again. Build and flash.
4. Switch the battery on.

The gap table from `cal` is stored in NVS (`odo` namespace) and survives
reflashing; only `idf.py erase-flash` removes it.

The motor test app does not use FG/SDA (A5), FLT, the odometer (D9) or the
TMP36 (A0). They may stay connected.

Speed is set as a PWM duty 0.0–1.0 over the serial monitor. Start with at least
0.08, lower to 0.065 for the slowest steady speed and stay at or below 0.7 (see
Measured performance).

> **The motor test app has no motor temperature, stall or speed limit
> protection.** Keep runs short and watch the motor. Use the configuration app
> for longer tests.

## Saved A89301 configuration

Four EEPROM words were changed from the configuration we received. All
Allegro-only bits are at their default values (checked by the programmer before
writing).

| EEPROM address | Original | Saved | Changed fields |
| --- | --- | --- | --- |
| 10 | `0x00DC` | `0x0108` | `RATED_CURRENT` 220 (2.5 A) → **264 (3.0 A)**, saved 2026-10-07 |
| 12 | `0x0028` | `0x01FF` | `PID_P` 40 → **255**, `MOTOR_INDUCTANCE` 0 → **1** |
| 13 | `0x011E` | `0x0114` | `PID_I` 30 → **20** |
| 21 | `0xC020` | `0xC120` | `SPEED_INPUT_OFF_THRESHOLD` 10 % → **6 %** |

To restore the original configuration, write the original values back to
addresses 10, 12, 13 and 21.

Full EEPROM contents after saving (addresses 8–22):

```
 8 0x275E    12 0x01FF    16 0x0A6C    20 0x0B25
 9 0x7120    13 0x0114    17 0x2000    21 0xC120
10 0x0108    14 0x0E15    18 0x8E0D    22 0x925E
11 0xD880    15 0x39B1    19 0x5500
```

Other relevant settings (unchanged): open loop speed control
(`SPEED_CLOSE_LOOP` 0), `RATED_SPEED` 1886 (1000 Hz), `RATED_VOLTAGE` 7.4 V,
`MOTOR_RESISTANCE` 113
(about 0.24 Ω phase to centre, matches the measurement), `FG_PIN_DIS` 1 (FG
always high, I2C friendly), `RESTART_ATTEMPT` 3 times, standby disabled.

## Tuning conclusions

**Symptom:** the motor accelerated for about 2 s and then stopped without a
fault. The chip stayed in state "spinning" and reported speeds up to its
limit (about 1080 Hz) while the wheel stood still.

**Cause:** the observer gains and the inductance did not fit this small
high-KV motor. The position estimate ran ahead of the rotor and the chip lost
the motor, without detecting it.

How it was found (logs with I2C readback and a wheel odometer):

1. Resistance, acceleration and rated speed had no or negative effect.
2. Allegro's diagnostic "open drive" (stepper mode, PID 0) ran steadily, which
   points to the observer (application note UM-A89301, step 16D).
3. PID sweeps: high `PID_P` and `PID_I` ran steadily with inductance 0, but at
   high motor current (about 4.5 A peak for 10 rev/s) and warm motor.
4. Inductance 1 was more efficient but needed a new PID: with inductance 1 the
   best combination is high `PID_P`, low `PID_I`. `PID_I` ≥ 80 stalled,
   `PID_I` 20 was the most stable.
5. Inductance 2 and higher failed at low speed.

**Result with inductance 1, PID 255/20:** steady from 0.5 to 6 m/s, in both
directions, with about half the motor current of the first stable setting.

## Measured performance (no load, 7.2 V at the board)

Speed is the chip estimate divided by 12, times the wheel circumference. It
matched the odometer where the odometer was reliable.

| Demand | Speed | Supply current | Power | Notes |
| --- | --- | --- | --- | --- |
| 0.06 | 0.5 m/s | 190 mA | 1.4 W | Only reachable after starting higher (see below). Close to the stop threshold |
| 0.065 | 0.57 m/s | 195 mA | 1.4 W | Recommended lowest speed |
| 0.08 | 0.77 m/s | 215 mA | 1.5 W | Lowest demand that starts from standstill |
| 0.12 | 1.26 m/s | 290 mA | 2.1 W | |
| 0.20 | 2.32 m/s | 430 mA | 3.1 W | |
| 0.30 | 3.61 m/s | 660 mA | 4.7 W | |
| 0.40 | 5.02 m/s | 890 mA | 6.3 W | |
| 0.70 | 6.03 m/s | 1150 mA | 8.1 W | Speed flattens above 0.40 in step tests |

- **Starting:** 12 of 12 starts from standstill were steady at 0.12–0.25.
  Startup (position detection) takes about 1.1 s at about 1.35 A.
- **Low speed:** the chip starts above 7.8 % demand and stops below 5.9 %.
  Start at 0.08, then lower the demand to 0.065 for about 0.57 m/s.
- **Energy:** about 0.33–0.46 Wh/km without load. The Raspberry Pi 5 will
  likely use more power than the drive motor.
- **Chip limit:** keep the electrical speed below about 1000 Hz (A89301 limit
  about 1085 Hz). The test apps stop at 1000 Hz.

## Safety

- **Pull resistors for driving (PWM wiring):** fit these before the car drives.

  ```text
                                   2V8 (A89301 board) ─┐
                                                      [R1] 12 kΩ
                                                       │
     D2 / GPIO5  ──────────────────────────────────────┴── BRAKE   (high = brake)
     D4 / GPIO7  ───────────────────────────────────────── DIR
     D5 / GPIO8  ──────────────────────────────────────┬── SPD/SCL (20 kHz PWM)
                                                       │   (no pull-up to 3V3 here)
                                                      [R2] 4.7 kΩ
                                                       │
                                   GND ────────────────┴── GND (header)
     D6 / GPIO9  ──────────────────────────────────────┬── steering servo signal
                                                      [R3] 10–12 kΩ (optional)
                                   GND ────────────────┘
  ```

  - **R1** keeps the brake on whenever the motor board has battery power and the
    ESP32 does not drive BRAKE (reset, flashing, crash, Nano unpowered). 2V8 is
    the A89301's own 2.8 V output, made for pull-ups (max 10 mA).
  - **R2** makes a floating SPD read as 0 % speed. 4.7 kΩ is stiff enough to
    win against a weak internal pull-up in the ESP32 during reset (a 100 kΩ
    pull-down would not), and costs the PWM output only about 0.7 mA. It never
    meets the 4.7 kΩ SCL pull-up, because each resistor stays at its own Nano pin.
  - **Keep the 4.7 kΩ SCL pull-up on A6**, never on D5 or the SPD wire: on SPD
    it turns a floating SPD into full speed. See Switching between the two wirings.
  - **R3** only stops the servo twitching during flashing.
  - Do not connect 2V8 or VM (battery voltage) to the Nano. **R1 goes to 2V8,
    never to VM** (the battery voltage pin on the same header): BRAKE is also wired to D2, so
    R1 on VM puts battery voltage on an ESP32 pin. Until 2026-10-07 R1 sat on
    VM, and with a test app that left D2 undriven the car drove off the stand.
  - **Check** with the Nano unplugged and the battery on: BRAKE ≈ 2.8 V,
    SPD ≈ 0 V. Then flash once on the stand; the wheels must not move.
  - **Checked 2026-10-07**, battery on, Nano on `VIN`, wheels in the air: SPD
    0 V throughout; BRAKE about 2.8 V while the ESP32 boots and its pins are
    released, and 3.3 V while RESET is held or the firmware drives it. The
    wheels stayed still in every phase.
- **BRAKE pull-up:** the board pulls BRAKE low (brake off) by default. While
  the ESP32 is reset or flashed its pins float, and in the I2C wiring SPD/SCL
  is pulled high, which the chip reads as a large speed demand. **The wheels
  spin during flashing.** R1, the 12 kΩ pull-up from BRAKE to 2V8, keeps the brake
  on whenever the ESP32 is not driving the pin. Recommended for ford.
- **Motor temperature:** the config app stops above 45 °C and waits for
  32 °C between runs. These limits are temporarily low because the TMP36 is
  taped over two layers of electrical tape and reads low and late. Raise them
  to 60 / 40 °C when the sensor has direct contact with the motor can.
  ManualByRemote reports the same sensor as `motor_temp_c` (16-read average,
  one read per 100 ms) and the web page colours it at the same limits
  (`MOTOR_WARM_C` 40 / `MOTOR_HOT_C` 45 in `tools/mqtt-ui/public/ford.mjs`).
  It only reports: the car does not brake on overtemperature.
- **Stall protection:** the config app brakes if the chip reports spinning
  above 50 Hz but the wheel gives no odometer pulse for 1 s.

## On the floor, loaded (2026-10-07)

Ford with the Pi and power bank aboard, drive battery 7.9–8.0 V, PWM on SPD,
measured by the odometer with `scripts/ai/raspberry-pi/creep_test.py` over the
Pi's UART. The firmware maps speed commands below 1 below `StartDuty` for this
(`dutyFor()` in `fordLogic.cpp`).

| Demand (duty) | Speed | Notes |
| --- | --- | --- |
| 0.099 (command 3) | starts in about 1.5 s | Reliable start from standstill on the flat |
| 0.072–0.089 | 0.48–0.6 m/s, flat | Typical; once 0.11 m/s at 0.087–0.089 after a fast ramp down |
| 0.064 | 0.16 m/s | After a gentle ramp down from 0.099 at 0.006/s |
| 0.056 | 0.11 m/s | The slowest held for a whole step |
| 0.048 | stall | |
| 0.068, in the air | 0.55 m/s, steady for 10 s | The same duty with no load: the motor is driven, faster |

- **0.1–0.2 m/s is reachable on the flat**, but with almost no torque in reserve:
  steering beyond about ±60 or the ramps stall it there.
- **Come down gently.** A drop in demand of 0.02/s stalled it more often than
  0.006/s.
- **Speed jumps about near the bottom.** The same demand gave 0.11 m/s in one
  run and 0.6 m/s in the next. The odometer's 53 mm pulses make single readings
  coarse at these speeds (2 pulses a second at 0.11 m/s).
- A PI loop on the odometer held 0.5 m/s through turns up to ±74 on the flat,
  and boosting the demand when a pulse came late caught the slowdowns before a
  ramp, but not the climb itself.

## Known issues and open items

- **Load test (2026-10-07):** with the Pi and power bank aboard, the car
  stalled at `RATED_CURRENT` 2.5 A: the current limit held `cmd` below the
  demand and the chip lost the motor. At 3.0 A, with a full battery, the belly
  clear of the floor and the front wheels held straight, 10 of 10 floor starts
  ran clean at I2C demand 0.1, 0.2 and 0.4. 3.0 A was saved. Demand 0.06–0.08
  on the floor is still untested.
- **Steering in the config app:** an unpowered servo lets the front wheels
  swing and the car turns and stalls, so the config app holds the MG90S
  centred; `servo <us>` sets a raw pulse.
- **Motor temperature under load:** the TMP36 reading drops out or jumps
  (`nan`, 54–101 °C) while the motor runs, so the overheat stop cannot be
  trusted during a drive. The 3.0 A current limit is the main motor protection.
- **Odometer:** misses pulses above about 2.5 m/s with the current mounting.
- **Speed plateau** above demand 0.40 in step tests is not explained. It does
  not matter at SLAM speeds.
- **Slow driving on the floor is limited by torque, not speed.** The 0.5 m/s in
  the table above is the motor with no load: the demand sets roughly a voltage,
  not a speed, so the loaded car on the floor goes slower at the same demand.
  See "On the floor, loaded" above. What it lacks down there is torque in reserve.
- **Ramps stall it from standstill**, even at demand 1.0: the 4–5 cm, 35 cm long
  bumps on the track (about 7°). Probably the 3.0 A `RATED_CURRENT` limit holding the motor
  back during start-up, so the chip loses it, as in the load test above; not measured on
  the ramp. FLT flashes. More current
  (the motor's temperature reading is not trustworthy under load, so with care),
  other gearing, or reaching the ramp already rolling are the ways over.
- **Speed control in the final car:** PWM on SPD (D5) for now (ManualByRemote
  uses it). I2C readback of speed, current and state is a later step.

## Tools

The A89301 configuration app (`A89301_CONFIG_MODE` in `firmware/main/source/main.cpp`,
code in `firmware/main/source/test_app/a89301_config_test.cpp`) reads, changes
and saves the settings over I2C. Type `h` for all commands. The most useful:

| Command | Purpose |
| --- | --- |
| `d` | Dump EEPROM and working registers with decoded fields |
| `m` | Live monitor: speed, wheel, currents, VBB, demand, state, temperature |
| `set <FIELD> <value>` | Change a field in the working register (lost at power cycle) |
| `run <L> <P> <I> [speed] [s]` | One test run with given inductance and PID |
| `profile up` / `profile start` | Speed steps / repeated starts, with power table |
| `save`, `save yes` | Show / program the working register changes into EEPROM |

Enable the ESP-IDF monitor log with Ctrl+T, Ctrl+L to keep a log file.

## References

- [Pololu A89301 motor controller](https://www.pololu.com/product/5357)
- [A89301 datasheet](https://www.pololu.com/file/0J2192/A89301-Datasheet.pdf)
- [A89301 application note UM-A89301](https://cdck-file-uploads-global.s3.dualstack.us-west-2.amazonaws.com/digikey/original/2X/e/e846acadea51a0fbdfe7fda1454ab9dd98a9ff01.pdf)
  (tuning procedure, state codes, Allegro-only bits)
