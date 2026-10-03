# Ford: Battery Meter

The Ford measures its drive battery (2S LiPo, 6.0–8.4 V) through a resistor
divider on `A3` (GPIO4). The firmware sends the voltage as `battery_v` in
`cnb/ford/telemetry`, and the Ford web page shows it on the BATTERY tile.

## Circuit

| Part | Nominal | Measured | From | To |
| --- | --- | --- | --- | --- |
| R1 | 100 kΩ | 101.24 kΩ | Battery + (after the switch) | `A3` |
| R2 | 33 kΩ | 32.99 kΩ | `A3` | `GND` |
| C1 | 150 nF (marked `154`) | 154 nF | `A3` | `GND` |

Battery voltage = pin voltage × (R1 + R2) / R2 ≈ pin voltage × 4.069, using
the measured values. C1 does not change the conversion; it gives the ADC a
steady charge to sample from. It is not a parameter in the code.

The meter averages its last 16 reads, one every 100 ms, so `battery_v` is
the average over about 1.6 s.

## Calibration points

Each row compares a multimeter on the battery terminals with `battery_v` at
the same moment. Add a row whenever you have a multimeter at hand,
preferably at other voltages, so the points spread over 6.0–8.4 V.

| Date | Multimeter (V) | `battery_v` (V) | Error (V) | Conditions |
| --- | --- | --- | --- | --- |
| 2026-10-03 | 7.38 | 7.34 | −0.04 (−0.5 %) | Not recorded. Firmware without correction. |

Write down the conditions (car standing still or driving, motors on or off),
because the voltage sags under load and the 1.6 s average lags behind it.

## Using the points

No correction is applied yet: −0.5 % is about 10 mV at the pin, which is
within the ESP32-S3 ADC's accuracy after its own calibration.

- **One point:** a gain factor, multimeter / `battery_v` (now 7.38 / 7.34 ≈ 1.005).
- **Two or more points, spread over the range:** a straight-line fit,
  multimeter = a × `battery_v` + b, which corrects both gain and offset.

The correction belongs where the battery is read in `fordLogic.cpp`, not in
`voltage_meter::Divider`, which stays a plain divider for any circuit.

A battery level (percent or time left) needs more than this: the LiPo's
discharge curve from voltage to charge. Calibrated voltages are the first
step towards it.
