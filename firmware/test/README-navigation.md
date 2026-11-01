# Navigation regression tests

Run `make -C test` and `make -C test navigation` from firmware.
The navigation tests reuse the parent integration test's hardware fakes,
but replace the previous steering/recovery expectations.

The live Vagrant logic now implements:
- Clamp every valid distance to reaction distance before any decision.
- Prefer straight ahead when forward clearance ties both sides.
- Otherwise choose the side with the greatest capped distance.
- Scale side steering with clearance advantage over forward; tiny differences
  no longer cause an immediate 30-degree turn.
- Use small filtered proportional corrections away from nearby walls, resetting
  the filter on a wall-side change or when both walls leave the correction range.
- Keep configured forward duty, with one 100 ms full-duty kick per explicit Start.
  Pauses and reversal do not rearm it.
- Use a short opposite-steered reverse pulse only when every capped route is
  at or below stop distance. An open side allows turning below the front threshold.
- Re-evaluate capped distances after reverse; no 55 cm opening requirement,
  forward recovery timeout or permanent attempt-budget latch.
- Resume the latest route after the reverse pause, without a forced forward turn
  toward an old exit. Equal side distances reuse the last side choice.
- Stop immediately on invalid sensors, Stop or lost authorization.
- Steering limits: 90 degrees for route selection, 45 for wall correction,
  and 60 for the first reverse. Continued blockage escalates reverse steering
  to 90 degrees; a clear route or reset clears escalation. Steering readiness
  remains mandatory before reversing.

Tests include the screenshot's exact distances and steady duty when armed,
and verify that an open left side does not bypass DISARMED authorization.
They also cover distant-value invariance, straight-ahead ties, both route signs,
wall correction, narrow corridors, front-stop hysteresis, steering without
braking, kick duration, MQTT Stop during a kick, sensor faults and mirrored
reverse/forward transitions, changed best routes during reverse, wall-side changes,
small side advantages and a grid of longest-direction choices.
Motor kick unit tests also cover timer wraparound
and manual-test bypass.

The old ReverseController remains in the repository for its existing tests;
the simplified live loop uses ReverseManeuver instead. The parent integration
test still has legacy assertions and is not validation of this navigation policy.
There is no rear obstacle or actual motion feedback: repeated reverse pulses
must be tested under supervision with an accessible stop.
