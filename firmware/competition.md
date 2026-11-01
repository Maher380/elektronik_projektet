# Vagrant run modes

Select the mode in `idf.py menuconfig`, under **Run mode**.

- Test: existing Wi-Fi/MQTT configuration, Start/Stop, manual tests and telemetry.
- Competition: no Wi-Fi/MQTT connection or communication manager. D10 (GPIO21)
  controls permission to run the same navigation and reverse logic.

Competition defaults: duty 44%, stop distance 45 cm, reaction distance 52 cm.
Change these in Run mode before building. Reaction must exceed stop distance.
Invalid configuration or failed GPIO initialization prevents driving.

## Wiring and start sequence

Connect the module's signal to D10 and share GND. The input must be 3.3 V logic,
never connect a 5 V signal directly. Use an external pull-down (e.g. 10 kohm to
GND) so a disconnected wire stays low. The existing GPIO input has no pull-down.

The firmware must first observe low, then high for at least 20 ms. High at boot
does not start the car. The first sampled low after starting cancels driving,
including an ongoing reverse manoeuvre. Stop is latched until firmware reboot.
Polling is every approximately 10 ms, subject to the duration of the loop.
Stop currently uses the existing zero-duty/coast output, not guaranteed active
electrical braking. Test stopping distance on a stand and then a clear floor.

Competition mode currently applies to Vagrant only; Ford/third logic remain
placeholders in this checkout. No flash or physical validation is automatic.

Start detection is adapted from PR #36, without importing unrelated Ford,
network, calibration or UI changes. No entire commit was cherry-picked.
