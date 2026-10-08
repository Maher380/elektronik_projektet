# Ford: Odometer

The Ford counts wheel rotation with an A3144 Hall-effect sensor on `D9` (GPIO18)
and **three magnets** on the **right rear** wheel — the only wheel measured; a
second sensor on the left rear is wanted but not fitted. The firmware sends `measured_speed_ms` and
`odometer_distance_m` in `cnb/ford/telemetry`.

| Property | Value | From |
| --- | --- | --- |
| Sensor pin | `D9` / GPIO18, input with internal pull-up | [pin_mapping.md](pin_mapping.md) |
| Magnets | 3: two opposite each other, a smaller third between them | Third added 2026-10-08; see History |
| Wheel diameter | 34 mm | `ford-build.md` in the host repo |
| Circumference | 106.81 mm | pi x 34 mm |
| Nominal distance per pulse | 35.60 mm | circumference / 3 |
| Measured gaps | 0.49 and 0.51 of a turn | Calibration log, 2026-10-05 |

The A3144 output is open-collector and active low: it pulls low while a magnet's
south pole is at the sensor. Pulses are counted on the falling edge by a GPIO
interrupt, so the main loop rate cannot make the firmware miss one. The internal
pull-up is required and no external one is. Both magnets must present the same
pole, since the A3144 is unipolar and a reversed pole reads as no magnet at all.

**Check the count before trusting anything else here.** Turn the wheel slowly by
hand in the odometer test app: ten turns must give exactly 20 pulses. A magnet
that passes too far from the sensor is simply never counted, and nothing in the
firmware can tell; see History.

## The layout

Two magnets are opposite each other, with a smaller third between them, so the gaps
are a quarter, a quarter and a half. `app::ford::DesignGapFractions` is
`{0.25, 0.25, 0.5}` — the design figures, placed by eye; GapCalibration measures what
the wheel actually has.

Uneven spacing is the point of the third magnet. The three rotations of
`{0.25, 0.25, 0.5}` are all distinct, so observed gaps match exactly one: checked
against `bestRotation` itself, the right rotation scores 0 and the runner-up 0.5, a
**margin of 0.500 against the 0.05 required**, from every starting rotation. The driver
can therefore phase-lock and time single gaps instead of whole revolutions.

Before 2026-10-08 the wheel had two even magnets, `{0.5, 0.5}`. Even gaps match every
rotation equally well, so the margin was 0, the driver never phased, and it measured
over whole revolutions throughout — exact, but slow to react.

## Speed

### Over a revolution

The driver times a whole revolution. One revolution covers the full circumference
wherever the magnets sit, so the spacing cancels **exactly** and nothing has to be
measured or calibrated. The window slides forward by one magnet on every pulse — the
driver keeps the last two pulse timestamps and compares the newest with the one two
pulses back — so the reading still updates twice per revolution.

Standing still restarts the window, so the stopped time is never averaged into the
speed after the car pulls away. Until the first revolution after a start or a reset
is complete, the driver falls back to timing the last gap. That assumes even
spacing, which on this wheel is right to about 1.4 %.

### What it costs

With three magnets the driver can phase-lock and time single gaps, so the lag is half a
gap rather than half a revolution. The quarter gaps are the quick ones; the half gap is no
worse than the old whole-revolution window.

| | at 0.3 m/s | at 0.5 m/s | at 0.8 m/s |
| --- | --- | --- | --- |
| Revolution | 356 ms | 214 ms | 134 ms |
| Pulse rate, 3 magnets | 8.4 Hz | 14.0 Hz | 22.4 Hz |
| Lag, phase-locked, quarter gap | about 44 ms | about 27 ms | about 17 ms |
| Lag, phase-locked, half gap | about 89 ms | about 53 ms | about 33 ms |
| Lag before, 2 even magnets | about 178 ms | about 107 ms | about 67 ms |

The 0.72 m/s column was measured with the wheel lifted: about 150 ms per revolution
at duty 0.08.

Three magnets still give fewer pulses than the six this wheel once had, and below
0.7 m/s the pulse rate is **under the 20 Hz floor ADR 0006 asked for a speed loop**.
Per-gap correction now cuts the lag, because the gaps are uneven and can be told apart,
but it does not lift the pulse rate. A speed loop on this odometer still has to be slow
at crawling speeds, and more magnets — all of them detected — would be the next gain.

### Per-gap correction and the gap calibration

The driver can do better than the window on an **unevenly** spaced wheel. Given a
measured gap table, it matches the gaps it observes against every rotation of the
table, and once one rotation clearly wins it times single gaps and scales each by
its own fraction. ADR 0008 explains the method, and the code still supports it.

On the three-magnet wheel it engages. The gaps are uneven, so one rotation wins
clearly, and the GapCalibration drive style — or `cal` in the A89301 configuration app —
is now **expected to succeed** and to produce the table the driver phases with.

That makes the calibration a test as well as a measurement. **Run it after any change to
the magnets, before trusting a drive.** If it still reports the magnets as too evenly
spaced, the third magnet is not being seen on every pass and the wheel is behaving as a
two-magnet one; if it reports that the speeds disagree, the magnet is seen only
sometimes, which is the worse case — see the note on `OdometerMagnets` in `ford.h`.

Each GapCalibration run logs every sampled revolution over serial on lines tagged
`CAL`, with the pulses counted, the time taken and the gaps. That log is how the
problems in History were found.

## Distance

Distance uses the nominal 35.60 mm per pulse — a circumference split three ways. Over
whole revolutions this is exact however the magnets sit, because three nominal pulses are
one real circumference. Between them it is only nominal, and on this deliberately uneven
wheel that matters more than it did: a quarter gap really covers 26.7 mm and the half gap
53.4 mm, so a single-pulse reading can be out by about 18 mm. The error does not
accumulate — it returns to exact at every full turn — and once GapCalibration has
measured the table the driver scales each gap by its own fraction.

An Odometer measures the wheel and not the ground, so a wheel that slips or locks
reads wrong and nothing here can tell that it has.

## Limits

**Noise filter.** Pulses closer together than 1 ms are discarded as contact noise.
The smallest gap is now a quarter turn, 26.7 mm, which takes 1 ms only at 26.7 m/s, so
the filter still cannot discard a real pulse anywhere in this car's speed range.

**Missed pulses at speed** were reported above about 2.5 m/s with the six-magnet
mounting. That has not been re-measured since, with either two magnets or three.

## History

**2026-10-08: a third magnet, and the gaps are uneven again.** A smaller magnet was glued
midway between the two, giving quarter, quarter, half. The point was the speed loop: at
0.3 m/s two magnets fed it at 5.6 Hz with about 178 ms of lag, which is far below the
20 Hz ADR 0006 asked for, and the crawl band is where the duty-to-speed gain is steepest
(0.16 m/s at duty 0.064, 0.5 m/s at 0.072) — a slow loop against a steep plant, which
hunts. Three uneven magnets lift the rate to 8.4 Hz and, once phased, cut the lag to
44–89 ms. `OdometerMagnets` and `DesignGapFractions` were changed to match; leaving them
at two would have made the distance read 1.5 times too high.

The wheel was first fitted with six magnets, deliberately uneven (gaps of 1/8, 1/8,
1/4, 1/4, 1/8, 1/8), so that per-gap correction could phase it. The gap calibration
failed with that layout, reporting the magnets as too evenly spaced. One magnet was
removed, leaving five, and it still failed.

The `CAL` log of a five-magnet run showed why. Every five-pulse window took the same
time and held exactly five pulses, but the gap pattern flipped between two shapes on
alternate windows. Exactly that happens when the sensor sees only **two** of the
magnets: a five-pulse window is then 2.5 revolutions, so successive windows start on
alternate magnets, and averaging them comes out flat. The magnets were glued at
different distances from the hub and not all the same way round, and three of them
did not trigger the sensor. The odometer had been reading speed and distance 2.5
times too low. The six-magnet failure was probably the same fault, though it was never
logged.

The undetected magnets were removed, and the two that remained were set opposite each
other.
