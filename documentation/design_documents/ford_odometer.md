# Ford: Odometer

The Ford counts wheel rotation with an A3144 Hall-effect sensor on `D9` (GPIO18)
and **four magnets** on a rear wheel. The firmware sends `measured_speed_ms` and
`odometer_distance_m` in `cnb/ford/telemetry`.

| Property | Value | From |
| --- | --- | --- |
| Sensor pin | `D9` / GPIO18, input with internal pull-up | [pin_mapping.md](pin_mapping.md) |
| Magnets | 4, spacing not assumed even | Fitted 2026-10-03 |
| Wheel diameter | 34 mm | `ford-build.md` in the host repo |
| Circumference | 0.1068 m | π × 34 mm |
| Nominal distance per pulse | 26.7 mm | circumference ÷ 4 |

The A3144 output is open-collector and active low: it pulls low while a magnet's
south pole is at the sensor. Pulses are counted on the falling edge by a GPIO
interrupt, so the main loop rate cannot make the firmware miss one. The internal
pull-up is required and no external one is.

## The magnets are not evenly spaced

They were fitted by hand, so the gaps between them differ. This matters for speed
but not much for distance.

**Speed is timed over a whole revolution, not over one gap.** Timing a single gap
and multiplying by the nominal 26.7 mm would report a speed that swings pulse to
pulse while the wheel turns at a perfectly constant rate, because a wide gap takes
longer than a narrow one. The error is the spacing error: magnets at 0°, 70°, 180°
and 250° would read 29 % high across each 70° gap and 18 % low across each 110° one,
alternating, at a dead constant wheel speed. A revolution, in contrast, covers the whole
circumference wherever the magnets sit, so the spacing cancels **exactly** and
nothing has to be measured or calibrated.

The window slides forward by one magnet on every pulse — the driver keeps the last
four pulse timestamps and compares the newest with the one four pulses back — so
the reading still updates four times per revolution. The cost is lag: the value is
an average over the last revolution, so it trails a real change in speed by up to
half a revolution. That is the price of not having to know where the magnets are.

Standing still restarts the window, so the stopped time is never averaged into the
speed after the car pulls away. Until the first revolution after a start or a reset
is complete, the driver falls back to timing the last gap, which *does* assume even
spacing and is an estimate only.

**Distance uses the nominal 26.7 mm per pulse.** Over whole revolutions this is
exact however the magnets sit. A part-revolution reading is off by at most the worst
spacing error — about 6 mm in the 0/70/180/250° example — and that error does not
accumulate, because it comes back to exact at every full turn.

## Limits

At 0.5 m/s the four magnets give **about 19 pulses per second**, against 4.7 for the
single magnet they replaced. ADR 0006 in the host repo names the magnets as a
precondition for the speed loop rather than an improvement to it, and asks for four to
six of them to close that loop at 20–30 Hz. Four is the bottom of that range: 19 Hz at
0.5 m/s is just under the 20 Hz figure, and the ADR's "roughly 28 pulses per second"
needs six. Four is enough to build the loop on; if it turns out to be marginal at low
speed, two more magnets are the cheapest fix, and the driver needs only its magnet
count changed because it never assumes the spacing is even.

Pulses closer together than 1 ms are discarded as contact noise. That only throws
away a real pulse if two magnets sit within about 8° of each other at 2.5 m/s, or
3.4° at 1 m/s, so it is safe for hand-fitted spacing but not for two magnets placed
almost on top of one another.

The mounting is known to miss pulses above roughly 2.5 m/s. That is a hardware
problem and is not fixed in software.
