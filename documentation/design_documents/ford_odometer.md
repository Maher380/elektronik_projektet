# Ford: Odometer

The Ford counts wheel rotation with an A3144 Hall-effect sensor on `D9` (GPIO18)
and **two magnets** on the **right rear** wheel — the only wheel measured; a
second sensor on the left rear is wanted but not fitted. The firmware sends `measured_speed_ms` and
`odometer_distance_m` in `cnb/ford/telemetry`.

| Property | Value | From |
| --- | --- | --- |
| Sensor pin | `D9` / GPIO18, input with internal pull-up | [pin_mapping.md](pin_mapping.md) |
| Magnets | 2, set opposite each other | Refitted 2026-10-05; see History |
| Wheel diameter | 34 mm | `ford-build.md` in the host repo |
| Circumference | 106.81 mm | pi x 34 mm |
| Nominal distance per pulse | 53.41 mm | circumference / 2 |
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

The two magnets are opposite each other, so the gaps are equal halves to within
about 1.4 %. `app::ford::DesignGapFractions` is `{0.5, 0.5}`.

Even spacing has one consequence: the two magnets cannot be told apart from their
timing, so the driver never knows which gap the wheel is in. It does not need to.
It measures over whole revolutions (below), which is exact however the magnets sit,
and even halves make the single-gap fallback accurate too.

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

| | at 0.5 m/s | at 0.72 m/s (duty 0.08, lifted) |
| --- | --- | --- |
| Revolution | 213.6 ms | 148 ms |
| Pulse rate | 9.4 Hz | 13.5 Hz |
| Lag of the revolution window | about 107 ms | about 74 ms |

The 0.72 m/s column was measured with the wheel lifted: about 150 ms per revolution
at duty 0.08.

Two magnets give fewer pulses than the six this wheel once had, and the pulse rate
is below the 20 Hz floor ADR 0006 asked for a speed loop. Per-gap correction cannot
cut the lag either, because it needs magnets that can be told apart. A speed loop on
this odometer has to be slow, or the wheel needs more magnets, all of them detected
and unevenly spaced.

### Per-gap correction and the gap calibration

The driver can do better than the window on an **unevenly** spaced wheel. Given a
measured gap table, it matches the gaps it observes against every rotation of the
table, and once one rotation clearly wins it times single gaps and scales each by
its own fraction. ADR 0008 explains the method, and the code still supports it.

On the current wheel it never engages. Even magnets match every rotation equally
well (a phase margin of 0, against the 0.05 required), so the driver stays on the
revolution window, which is exact. For the same reason the gap calibration — the
GapCalibration drive style, or `cal` in the A89301 configuration app — always fails
on this wheel, reporting that the speeds disagree or that the magnets are too evenly
spaced. That is expected: this wheel needs no calibration.

Each GapCalibration run logs every sampled revolution over serial on lines tagged
`CAL`, with the pulses counted, the time taken and the gaps. That log is how the
problems in History were found.

## Distance

Distance uses the nominal 53.41 mm per pulse. Over whole revolutions this is exact
however the magnets sit, because two nominal pulses are one real circumference. A
half-revolution reading is off by about 1 mm (0.01 of a turn), and the error does
not accumulate: it returns to exact at every full turn.

An Odometer measures the wheel and not the ground, so a wheel that slips or locks
reads wrong and nothing here can tell that it has.

## Limits

**Noise filter.** Pulses closer together than 1 ms are discarded as contact noise.
The gaps are 53 mm, which takes 1 ms only at 53 m/s, so the filter cannot discard a
real pulse anywhere in this car's speed range.

**Missed pulses at speed** were reported above about 2.5 m/s with the six-magnet
mounting. That has not been re-measured with two magnets.

## History

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
