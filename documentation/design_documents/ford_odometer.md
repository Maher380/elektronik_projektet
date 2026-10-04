# Ford: Odometer

The Ford counts wheel rotation with an A3144 Hall-effect sensor on `D9` (GPIO18)
and **six magnets** on the **right rear** wheel — the only wheel measured; a
second sensor on the left rear is wanted but not fitted. The firmware sends `measured_speed_ms` and
`odometer_distance_m` in `cnb/ford/telemetry`.

| Property | Value | From |
| --- | --- | --- |
| Sensor pin | `D9` / GPIO18, input with internal pull-up | [pin_mapping.md](pin_mapping.md) |
| Magnets | 6: four smaller, two slightly larger | Fitted 2026-10-04 |
| Wheel diameter | 34 mm | `ford-build.md` in the host repo |
| Circumference | 106.81 mm | pi x 34 mm |
| Nominal distance per pulse | 17.80 mm | circumference / 6 |

The A3144 output is open-collector and active low: it pulls low while a magnet's
south pole is at the sensor. Pulses are counted on the falling edge by a GPIO
interrupt, so the main loop rate cannot make the firmware miss one. The internal
pull-up is required and no external one is. All six magnets must present the same
pole, since the A3144 is unipolar and a reversed pole reads as no magnet at all.

## The layout

Four magnets were placed first, roughly a quarter-turn apart. Two slightly larger
ones were then added at the midpoints of the two gaps either side of *one* of the
originals, so that original has a larger magnet on each side.

```
        A0   <- the flanked magnet
      /    \
    B        B        cyclic order:  A0  B  A1  A2  A3  B
    |        |        gap (deg):      45  45  90  90  45  45
    A1       A3       fraction:     .125 .125 .25 .25 .125 .125
      \    /          distance:     13.4 13.4 26.7 26.7 13.4 13.4 mm
        A2
```

The gaps sum to exactly 1 by construction, whatever the real angles turn out to
be. The figures above are the design intent, measured by eye; the real ones come
from the calibration session.

**The unevenness is deliberate and it is useful.** The sequence of six fractions
has no rotational symmetry, so observing one revolution and comparing against a
stored table identifies which gap the wheel is in, from timing alone. A wheel with
six evenly spaced magnets would be the harder case, not the easier one: every
rotation would fit equally well and the phase would be unrecoverable.

> The two added magnets are the **larger** pair and they sit in the narrow gaps, so
> they are only 13.4 mm from their neighbours. A larger magnet is detected over a
> wider arc, so the risk is that two pulses merge into one. Confirm the count rises
> by exactly 6 per turn before trusting anything else here.

## Speed

### Over a revolution, not over a gap

Timing one gap and multiplying by the nominal 17.80 mm reports a speed that swings
with the spacing rather than with the car: at a dead constant wheel speed the four
narrow gaps read **33 % high** and the two wide ones **33 % low**, alternating.

So the driver times a whole revolution. One revolution covers the full
circumference wherever the magnets sit, so the spacing cancels **exactly** and
nothing has to be measured or calibrated. The window slides forward by one magnet
on every pulse — the driver keeps the last six pulse timestamps and compares the
newest with the one six pulses back — so the reading still updates six times per
revolution.

Standing still restarts the window, so the stopped time is never averaged into the
speed after the car pulls away. Until the first revolution after a start or a reset
is complete, the driver falls back to timing the last gap, which *does* assume even
spacing and is an estimate only.

### What it costs

Averaging over a revolution is exact but late. At 0.5 m/s:

| | value |
| --- | --- |
| Revolution | 213.6 ms |
| Lag of the revolution window | about **107 ms** (half a revolution) |
| Lag of a per-gap reading | 13.4 ms narrow, 26.7 ms wide |

A speed loop at the 20-30 Hz asked for in ADR 0006 has a 33-50 ms cycle, so 107 ms
is two to three cycles of pure phase lag and the loop has to be detuned to stay
stable. Correcting each gap with its measured fraction removes the averaging and
brings the lag inside one cycle. That is what the calibration is for; see ADR 0008
in the host repo.

### Pulse rate is not steady

Six magnets give **28.1 pulses per second** at 0.5 m/s on average, against 4.7 for
the single magnet they replaced. But the gaps are uneven, so the instantaneous rate
alternates:

| Gap | Interval at 0.5 m/s | Instantaneous rate |
| --- | --- | --- |
| Narrow (.125) | 26.7 ms | 37.4 Hz |
| Wide (.25) | 53.4 ms | 18.7 Hz |
| Average | 35.6 ms | 28.1 Hz |

**The 18.7 Hz figure is the one a control loop has to survive**, and it is below the
20 Hz floor ADR 0006 asked for. The average meets the target and the worst case does
not.

### Per-gap correction

Given a measured gap table, the driver does better than the window. Once it knows
which gap the wheel is in, it times that single gap and scales it by that gap's own
fraction, so the lag drops from 107 ms to 13-27 ms. See ADR 0008 for why.

**Knowing which gap** is the hard part: one sensor and six magnets give no datum.
The driver recovers it by matching the gaps it observes against every rotation of
the stored table and taking the best fit, but only when the fit beats the runner-up
by a clear margin. Ford's layout scores about 0.25; evenly spaced magnets score 0
and are deliberately never phased. It then requires **three windows a whole
revolution apart** to agree before it trusts the answer, because the window slides
by one magnet per pulse, so consecutive windows share five of their six gaps and
would repeat each other's mistakes rather than confirm them. In simulation that
took the rate of wrong phase locks to zero. The phase settles after three
revolutions.

**It falls back to the window whenever it is unsure**: the first revolutions after
starting, a direction change, a standstill, a missed pulse, or gaps that stop
matching the table. Every loss is counted and reported as `odometer_phase_losses`,
and `measured_speed_source` says which reading is live (`per_gap` or `revolution`).
The reading is therefore at worst late, never wrong.

**Reverse always uses the window.** The table describes the gaps in forward order,
and Ford's gap pattern is a palindrome, so reversing cannot be detected from the
timing either. Rather than risk a reading wrong by a third, the slower correct one
is used.

### Measuring the gaps

`cal yes` in the A89301 configuration app. **Lift the car first** — it spins the
wheel under power for about a minute. The app drives the motor over I2C, so the
SPD/SCL wire has to move first; the steps are in "Switching between the two
wirings" in `ford_a89301_motor_controller.md`.

It measures at three duties and stores the result only if the three agree to within
0.01 of a revolution per gap. That gate is the point of the session, not a
formality: Ford turns 12 motor commutations per wheel revolution against 6 magnets,
so the motor's roughness falls at the same wheel angles on every revolution.
Averaging more revolutions does not remove it — the average converges on the wrong
answer with a shrinking variance, which looks like a good measurement. Geometry
does not change with speed and the effect of torque ripple does, so disagreement
between speeds *is* the ripple, measured directly.

It also refuses a table it could never phase, and one that does not sum to 1. On
any refusal the previous table is left alone. The table and the spread it was
measured with live in the `odo` NVS namespace; with none stored the firmware uses
the design values above, which already cut the error from 33 % to about 11 %.

Recalibrate after touching the wheel. Nothing on the car can tell that a magnet
moved from a table that was correct when it was written.

## Distance

Distance uses the nominal 17.80 mm per pulse. Over whole revolutions this is exact
however the magnets sit, because six nominal pulses are one real circumference. A
part-revolution reading is off by at most **8.90 mm**, and the error does not
accumulate — it returns to exact at every full turn:

| After pulse | 1 | 2 | 3 | 4 | 5 | 6 |
| --- | --- | --- | --- | --- | --- | --- |
| Error (mm) | +4.45 | +8.90 | 0 | -8.90 | -4.45 | 0 |

Note this got **worse** with six magnets, not better: the four original magnets gave
about 5.9 mm, and the deliberate 2:1 spacing gives 8.90 mm. Six magnets traded a
little distance accuracy for pulse rate, and the gap table buys it back.

An Odometer measures the wheel and not the ground, so a wheel that slips or locks
reads wrong and nothing here can tell that it has.

## Limits

**Noise filter.** Pulses closer together than 1 ms are discarded as contact noise.
The narrowest gap is 13.35 mm, which takes 1 ms only at 13.4 m/s, so the filter
cannot discard a real pulse anywhere in this car's speed range. The thing that can
is two magnets whose detection arcs overlap, which is a mounting problem rather than
a timing one.

**Missed pulses above about 2.5 m/s** with the current mounting. That is a hardware
problem and is not fixed in software. It costs distance, and once per-gap correction
is in use it also costs the phase, which is why the driver falls back to the
revolution window whenever it is unsure of its place.
