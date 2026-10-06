// Ford ManualByRemote page: two sliders, also driven by the arrow keys and the on-screen pad,
// streamed to the car while this tab is in control.
// @todo Add tests for the speed hold after start, for the arrow-key steps
// and for stopping the stream when control ends.
import { FordChart, WINDOW_MS } from '/fordcharts.mjs';
const $ = id => document.getElementById(id);
// The Ford's real wheel angle at full lock, so a steering command (a share of full lock) can be
// shown in degrees beside the wheel angle the Pi measures.
// @todo Measure the Ford's full-lock wheel angle; 25 is a placeholder.
const FULL_LOCK_DEG = 25;
// A wheel angle further than this from the command is flagged.
const WHEEL_DIFFERS_DEG = 5;
const PI_INTERVAL_MS = 200;
// Drive battery, a 2S LiPo. The bar spans BATTERY_MIN_V to BATTERY_MAX_V.
const BATTERY_MIN_V = 5, BATTERY_MAX_V = 9, BATTERY_CELLS = 2;
// At or below these the battery is low (stop soon) or critical (stop now, before the cells are damaged).
const BATTERY_LOW_V = 6.8, BATTERY_CRITICAL_V = 6.4;
const batteryLabels = { ok: 'OK', low: 'LOW · stop soon', critical: 'CRITICAL · stop now' };
// Motor can temperature from the TMP36 on A0. HOT is the A89301 config app's stop limit; both are
// low while the sensor sits on electrical tape and reads low and late (ford_a89301_motor_controller.md).
const MOTOR_WARM_C = 40, MOTOR_HOT_C = 45;
const batteryPercent = volts => Math.min(100, Math.max(0, (volts - BATTERY_MIN_V) / (BATTERY_MAX_V - BATTERY_MIN_V) * 100));
const slamLabels = { tracking: 'TRACKING', lost: 'TRACKING LOST', starting: 'STARTING' };
// crypto.randomUUID() only exists on https and localhost; getRandomValues also works over plain http on the LAN.
const client = Array.from(crypto.getRandomValues(new Uint8Array(16)), b => b.toString(16).padStart(2, '0')).join('');
const motorLabels = { braked: 'BRAKED', no_drive: 'NO DRIVE', braking: 'BRAKING', driving_forward: 'DRIVING FWD', driving_reverse: 'DRIVING REV' };
let state = null, csrf = null, stream = null, connected = false, received = 0, pending = 0, feedbackUntil = 0;
// After every start the car gets speed 0 until the operator moves the speed slider.
let speedHeld = true;
const text = (id, value) => { $(id).textContent = value; };
const commandDeg = command => command * FULL_LOCK_DEG / 90;
const fixed = (n, digits, unit) => typeof n === 'number' && Number.isFinite(n) ? `${n > 0 ? '+' : ''}${n.toFixed(digits)}${unit}` : '—';
const signed = n => typeof n === 'number' && Number.isFinite(n) ? (n > 0 ? '+' : '') + Math.round(n) : '—';
const timestamp = () => state ? state.now + performance.now() - received : Date.now();
function message(value, type = '', duration = 7000) {
  text('feedback', value); $('feedback').className = 'feedback ' + type; feedbackUntil = performance.now() + duration;
}
function accept(next) { state = next; received = performance.now(); connected = true; }
async function post(action, extra = {}, keepalive = false) {
  const response = await fetch('/api/' + action, { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Cnb-Token': csrf }, body: JSON.stringify({ client, ...extra }), keepalive });
  const result = await response.json();
  if (!response.ok) throw Error(result.error || 'Request failed.');
  if (result.state) accept(result.state);
  return result;
}
async function act(action) {
  pending++; message('Waiting for acknowledgement from the car…', '', 6000);
  try { await post(action); message(state.notice, 'success'); return true; }
  catch (error) { message(error.message, 'error', 12000); return false; }
  finally { pending--; }
}
const controlling = () => state?.owner?.client === client && state.owner.armed && !document.hidden;
function sendDrive() {
  if (!controlling()) return;
  const speed = speedHeld ? 0 : Number($('speed').value);
  void post('drive', { steering: Number($('steering').value), speed }).catch(() => { connected = false; });
}
function centre(id) { $(id).value = 0; $(id).dispatchEvent(new Event('input')); }
// Hold before the request: the drive stream can begin before the start reply arrives.
$('start').addEventListener('click', () => { speedHeld = true; void act('start'); });
// Mirror the car's style until the operator picks one, so the box never claims a
// selection the car is not on.
let styleTouched = false;
$('style-select').addEventListener('change', () => { styleTouched = true; });
$('style-apply').addEventListener('click', async () => {
  pending++;
  message('Waiting for acknowledgement from the car…', '', 6000);
  try { await post('drive-style', { style: $('style-select').value }); message(state.notice, 'success'); }
  catch (error) { message(error.message, 'error', 12000); }
  finally { pending--; }
});
$('cal-store').addEventListener('click', async () => {
  pending++;
  message('Storing the measured gap table…', '', 6000);
  try { await post('store-gaps'); message(state.notice, 'success'); }
  catch (error) { message(error.message, 'error', 12000); }
  finally { pending--; }
});
// Arrow keys and the on-screen pad step a slider per press: speed by 1, steering by 10 degrees.
// Holding a key repeats at the keyboard's rate. The value stays where it is when the key is released.
// The steering step is set on the page (1-45) and remembered in this browser.
const STEER_STEP_KEY = 'ford.steeringStep';
const steeringStep = () => { const n = Math.round(Number($('steer-step').value)); return Number.isFinite(n) ? Math.min(45, Math.max(1, n)) : 10; };
try { const saved = localStorage.getItem(STEER_STEP_KEY); if (saved) $('steer-step').value = saved; } catch { /* Storage blocked: keep 10. */ }
$('steer-step').addEventListener('change', () => {
  $('steer-step').value = steeringStep();
  try { localStorage.setItem(STEER_STEP_KEY, String(steeringStep())); } catch { /* Not remembered; still used. */ }
});
const pad = { up: ['speed', 1], down: ['speed', -1], left: ['steering', -1], right: ['steering', 1] };
const arrows = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right' };
function step(name) {
  const [id, direction] = pad[name];
  const change = id === 'steering' ? direction * steeringStep() : direction;
  // The range input clamps to its min/max.
  $(id).value = Number($(id).value) + change;
  $(id).dispatchEvent(new Event('input'));
}
// The 0 key (main row or numpad) sets the speed to 0; steering stays where it is.
document.addEventListener('keydown', event => {
  if (event.altKey || event.ctrlKey || event.metaKey) return;
  // Typing a step (e.g. 10) must not zero the speed or steer the car.
  if (event.target === $('steer-step')) return;
  if (event.key === '0') { event.preventDefault(); centre('speed'); return; }
  const name = arrows[event.key];
  if (!name) return;
  // Without this a focused slider would also move itself, giving two steps per press.
  event.preventDefault(); step(name);
});
for (const button of document.querySelectorAll('[data-pad]')) button.addEventListener('click', () => step(button.dataset.pad));
// Panic stop zeroes both sliders first, so they are at rest even if the stop request fails.
$('panic').addEventListener('click', () => {
  $('steering').value = 0; $('speed').value = 0; speedHeld = true;
  void act('stop');
});
// Sliders only change the values; the 100 ms stream below sends them. Sending on every
// input event floods the car's 8-message receive queue and it disarms (message_overflow).
$('speed').addEventListener('input', () => { speedHeld = false; });
for (const id of ['steering', 'speed']) {
  $(id).addEventListener('dblclick', () => centre(id));
  $(id + '-zero').addEventListener('click', () => centre(id));
}
function chip(id, label, good, warning = false) {
  const item = $(id); item.className = 'chip ' + (good ? 'good' : warning ? 'warn' : 'neutral'); item.querySelector('strong').textContent = label;
}
// 30 seconds of samples for the charts. Each snapshot carries the last 10 seconds, so new
// samples are appended by their receive time.
const buffer = { car: [], pi: [] };
function remember(now) {
  for (const [key, samples] of [['car', state.history], ['pi', state.pi?.history]]) {
    const list = buffer[key], last = list.at(-1)?.at ?? 0;
    for (const sample of samples || []) if (sample.at > last) list.push(sample);
    buffer[key] = list.filter(sample => sample.at >= now - WINDOW_MS);
  }
}
const steeringChart = new FordChart('steering-chart', [
  { color: '#48d9e8', side: 'left', format: v => Math.round(v) + '°', range: () => [-FULL_LOCK_DEG * 1.4, FULL_LOCK_DEG * 1.4] },
  { color: '#b4e36c', side: null, range: () => [-FULL_LOCK_DEG * 1.4, FULL_LOCK_DEG * 1.4] },
]);
// SLAM and the odometer share the right-hand m/s axis, so both lines can be compared directly.
let measuredTop = 1;
const measuredRange = () => [-measuredTop, measuredTop];
const speedChart = new FordChart('speed-chart', [
  { color: '#48d9e8', side: 'left', format: v => String(Math.round(v)), range: () => [-100, 100] },
  { color: '#b4e36c', side: 'right', format: v => v.toFixed(1), range: measuredRange },
  { color: '#c79bff', side: null, range: measuredRange },
]);
const number = n => typeof n === 'number' && Number.isFinite(n) ? n : null;
const speedSources = { per_gap: 'per gap', revolution: 'revolution window', none: 'no reading yet' };
function drawCharts(now) {
  const car = (pick) => buffer.car.map(s => ({ at: s.at, value: number(pick(s.data)) }));
  const pi = (pick) => buffer.pi.map(s => ({ at: s.at, value: number(pick(s.data)) }));
  const command = car(d => typeof d.steering_deg === 'number' ? commandDeg(d.steering_deg) : null);
  const wheel = pi(d => d.wheel_angle_deg?.slam);
  steeringChart.draw([command, wheel], now, `Steering, last 30 seconds: command ${fixed(command.at(-1)?.value, 1, '°')}, wheel angle ${fixed(wheel.at(-1)?.value, 1, '°')}`);
  const speed = car(d => d.motor?.speed_command), measured = pi(d => d.measured_speed_mps?.slam);
  const odometer = car(d => d.measured_speed_ms);
  measuredTop = Math.max(1, ...[...measured, ...odometer].map(p => Math.abs(p.value ?? 0)));
  speedChart.draw([speed, measured, odometer], now, `Speed, last 30 seconds: command ${signed(speed.at(-1)?.value)}, measured ${fixed(measured.at(-1)?.value, 2, ' m/s')}, odometer ${fixed(odometer.at(-1)?.value, 2, ' m/s')}`);
}
// Odometer on the right rear wheel. Its speed has no sign: it cannot see which way the wheel turns.
function renderOdometer(fresh, data) {
  const speed = fresh ? number(data.measured_speed_ms) : null, distance = fresh ? number(data.odometer_distance_m) : null;
  text('odometer-speed', speed === null ? '—' : `${speed.toFixed(2)} m/s`);
  const losses = number(data.odometer_phase_losses);
  text('odometer-detail', !fresh ? 'Odometer: waiting for telemetry'
    : speed === null && distance === null ? 'Odometer: not in telemetry (not fitted or failed to start)'
    : `Odometer: ${distance === null ? '—' : distance.toFixed(2) + ' m'} · ${speedSources[data.measured_speed_source] || data.measured_speed_source || '—'}`
      + (losses === null ? '' : ` · ${losses} phase loss${losses === 1 ? '' : 'es'}`));
}
function renderPi(now, serverFresh, carSteering) {
  const pi = state.pi || {}, data = pi.telemetry || {};
  const fresh = serverFresh && pi.fresh && now - pi.receivedAt <= PI_INTERVAL_MS * 2.5;
  text('pi-state', fresh ? slamLabels[data.slam_state] || '—' : pi.online ? 'STALE' : 'OFFLINE');
  $('pi-state').className = fresh && data.slam_state === 'tracking' ? 'armed' : fresh && data.slam_state === 'lost' ? 'alert' : '';
  const temp = fresh ? data.cpu_temp_c : null;
  text('pi-temp', temp === null || temp === undefined ? 'CPU —' : `CPU ${temp.toFixed(1)} °C${temp >= 80 ? ' · throttling, SLAM may lag' : ''}`);
  $('pi-temp').className = temp >= 80 ? 'hot' : temp >= 70 ? 'warm' : '';
  const wheel = fresh ? number(data.wheel_angle_deg?.slam) : null;
  const measured = fresh ? number(data.measured_speed_mps?.slam) : null;
  text('wheel-value', fixed(wheel, 1, '°'));
  text('measured-speed', measured === null ? '—' : `${measured.toFixed(2)} m/s`);
  const asked = carSteering === null || carSteering === undefined ? null : commandDeg(carSteering);
  text('wheel-detail', `Command ≈ ${fixed(asked, 1, '°')} at full lock ${FULL_LOCK_DEG}°` + (fresh && wheel === null && data.slam_state === 'tracking' ? ' · wheel angle unknown while slow' : ''));
  $('wheel-value').classList.toggle('differs', wheel !== null && asked !== null && Math.abs(wheel - asked) > WHEEL_DIFFERS_DEG);
}
function renderBattery(fresh, data) {
  const volts = fresh && typeof data.battery_v === 'number' && Number.isFinite(data.battery_v) ? data.battery_v : null;
  const level = volts === null ? null : volts <= BATTERY_CRITICAL_V ? 'critical' : volts <= BATTERY_LOW_V ? 'low' : 'ok';
  text('battery', volts === null ? '—' : `${volts.toFixed(2)} V`);
  $('battery').className = 'mono' + (level === 'critical' ? ' hot' : level === 'low' ? ' alert' : '');
  text('battery-detail', volts !== null ? `${(volts / BATTERY_CELLS).toFixed(2)} V/cell · ${batteryLabels[level]}`
    : fresh ? 'No battery reading' : 'Waiting for telemetry');
  $('battery-fill').style.width = volts === null ? '0' : batteryPercent(volts) + '%';
  $('battery-fill').className = level || '';
}
function renderMotorTemp(fresh, data) {
  const temp = fresh && typeof data.motor_temp_c === 'number' && Number.isFinite(data.motor_temp_c) ? data.motor_temp_c : null;
  const level = temp === null ? null : temp >= MOTOR_HOT_C ? 'hot' : temp >= MOTOR_WARM_C ? 'warm' : 'ok';
  text('motor-temp', temp === null ? '—' : `${temp.toFixed(1)} °C`);
  $('motor-temp').className = 'mono' + (level === 'hot' ? ' hot' : level === 'warm' ? ' alert' : '');
  text('motor-temp-detail', level === 'hot' ? 'HOT · stop and let it cool' : level === 'warm' ? 'WARM · ease off'
    : level === 'ok' ? 'Motor can · OK' : fresh ? 'No temperature reading' : 'Waiting for telemetry');
  $('motor-temp-detail').className = level === 'hot' ? 'hot' : level === 'warm' ? 'warm' : '';
}
const styleLabels = { manual_by_remote: 'ManualByRemote', gap_calibration: 'GapCalibration', speed_calibration: 'SpeedCalibration' };
const calPhaseLabels = { idle: 'IDLE', settling: 'SETTLING', sampling: 'MEASURING', measured: 'MEASURED', failed: 'REFUSED' };
// Why a run produced no table. These are the operator's next action, not an error code:
// a disagreement means the magnets, a stall means the wheel, too_hot means wait.
const calFailureDetail = {
  no_odometer: 'No odometer. Check the A3144 sensor on D9.',
  stalled: 'The wheel stopped turning. Is the battery on and the wheel free?',
  too_hot: 'The motor can got too hot. Let it cool, then measure again.',
  disagreed: 'A gap changed with speed, so that is the motor and not the wheel. Space the magnets more unevenly, then measure again.',
  not_plausible: 'The averaged table is not usable. Measure again.',
  thin_phase_margin: 'These magnets are too evenly spaced to tell one gap from another. Space them more unevenly.',
  stopped: 'Stopped before it finished, so nothing was measured. A part-measured wheel is not a calibration.',
};
function renderCalibration(fresh, data, armed) {
  const cal = fresh ? data.calibration : null;
  $('calibration-panel').hidden = !cal;
  if (!cal) return;
  const phase = cal.phase || 'idle';
  text('cal-phase', calPhaseLabels[phase] || phase.toUpperCase());
  $('cal-phase').className = 'chip ' + (phase === 'measured' ? 'good' : phase === 'failed' ? 'warn' : 'neutral');
  const dutyCount = cal.duty_count || 0;
  text('cal-duty', dutyCount ? `${Math.min((cal.duty_index || 0) + 1, dutyCount)} of ${dutyCount}` : '—');
  text('cal-turns', cal.revolutions_wanted ? `${cal.revolutions || 0} / ${cal.revolutions_wanted}` : '—');
  text('cal-spread', Number.isFinite(cal.spread) ? cal.spread.toFixed(4) : '—');
  text('cal-margin', Number.isFinite(cal.phase_margin) ? cal.phase_margin.toFixed(4) : '—');
  const gaps = Array.isArray(cal.gaps) ? cal.gaps : [];
  $('cal-gaps').textContent = gaps.length ? gaps.map(gap => gap.toFixed(4)).join('   ') : '';
  let detail;
  if (phase === 'measured') detail = cal.stored ? 'Stored. It takes effect when the car next restarts.' : 'Measured. Check the spread, then confirm to store it.';
  else if (phase === 'failed') detail = calFailureDetail[cal.failure] || 'The run produced no table.';
  else if (phase === 'settling') detail = 'Letting the wheel speed steady before measuring.';
  else if (phase === 'sampling') detail = 'Averaging whole revolutions at this speed.';
  else detail = 'Press Start with the car lifted to measure.';
  if (cal.store_failed) detail = 'Writing to flash failed. The measured table is still waiting; try again.';
  if (!cal.overheat_guard && (phase === 'settling' || phase === 'sampling')) {
    detail += ' No motor temperature sensor, so the overheat guard is off.';
  }
  text('cal-detail', detail);
  $('cal-detail').className = 'measured-detail' + (phase === 'failed' || cal.store_failed ? ' hot' : '');
  $('cal-store').disabled = pending > 0 || armed || phase !== 'measured' || cal.stored === true;
}
// Speed calibration. The car sends only its latest finished leg, so the console server
// collects the legs of a run (state.speed_log) and also writes them to a CSV file.
const spdPhaseLabels = { idle: 'IDLE', driving: 'DRIVING', braking: 'BRAKING', finished: 'FINISHED', failed: 'STOPPED' };
const spdResultLabels = {
  reached: 'reached', short: 'held briefly', not_reached: 'not reached', lowest: 'stalled below',
  bottom: 'rolled at every step', no_start: 'no start', stalled: 'stalled',
};
const spdGood = ['reached', 'lowest', 'bottom'];
const spdFailureDetail = {
  no_odometer: 'No odometer. Check the A3144 sensor on D9.',
  too_hot: 'The motor can got too hot. Let it cool, then run again.',
  stopped: 'Stopped before the last leg. The legs below were measured; Start runs all of them again.',
};
const spdTarget = (from, target) => from > 0 ? `${from.toFixed(1)}→${target.toFixed(1)}` : target > 0 ? target.toFixed(1) : 'lowest';
const spdFixed = (n, digits) => typeof n === 'number' && Number.isFinite(n) ? n.toFixed(digits) : '—';
let speedTableKey = '';
function renderSpeedTable(log) {
  const body = $('spd-rows');
  body.replaceChildren();
  if (!log.legs.length) {
    const cell = body.insertRow().insertCell(); cell.colSpan = 10; cell.textContent = 'No legs yet';
  }
  for (const leg of log.legs) {
    const row = body.insertRow();
    const add = (value, className = '') => { const cell = row.insertCell(); cell.textContent = value; cell.className = className; };
    add(String(leg.leg + 1));
    add(spdTarget(leg.from_ms ?? 0, leg.target_ms ?? 0));
    add(leg.direction === 'forward' ? 'FWD' : 'BACK');
    add(spdResultLabels[leg.result] || leg.result, spdGood.includes(leg.result) ? 'measured' : 'warn');
    add(spdFixed(leg.speed_ms, 3), leg.speed_ms === null ? 'missing' : '');
    add(spdFixed(leg.duty, 3), leg.duty === null ? 'missing' : '');
    add(spdFixed(leg.rise_s, 2), leg.rise_s === null ? 'missing' : '');
    add(spdFixed(leg.overshoot_ms, 3), leg.overshoot_ms === null ? 'missing' : '');
    add(spdFixed(leg.stop_m, 2));
    const off = leg.error_m;
    add(off === null ? '—' : `${off > 0 ? '+' : ''}${off.toFixed(2)} m`, off !== null && Math.abs(off) > 0.15 ? 'warn' : '');
  }
  $('spd-csv').value = log.csv || '';
}
function renderSpeedCalibration(fresh, data) {
  const spd = fresh ? data.speed_calibration : null;
  const log = state.speed_log || { legs: [], csv: '', file: null, error: null };
  $('speed-panel').hidden = !spd && !log.legs.length;
  const key = log.legs.map(leg => leg.leg).join(',') + '|' + log.legs.length;
  if (key !== speedTableKey) { speedTableKey = key; renderSpeedTable(log); }
  text('spd-file', log.error ? log.error : log.file ? `Saved as tools/mqtt/logs/${log.file}` : '');
  $('spd-file').className = 'measured-detail' + (log.error ? ' hot' : '');
  if (!spd) {
    text('spd-phase', 'NOT SELECTED'); $('spd-phase').className = 'chip neutral';
    text('spd-detail', 'The results of the last run. Choose SpeedCalibration to run it again.');
    return;
  }
  const phase = spd.phase || 'idle';
  const running = phase === 'driving' || phase === 'braking';
  text('spd-phase', spdPhaseLabels[phase] || phase.toUpperCase());
  $('spd-phase').className = 'chip ' + (phase === 'finished' ? 'good' : phase === 'failed' ? 'warn' : 'neutral');
  text('spd-leg', spd.leg_count ? `${Math.min(spd.leg + 1, spd.leg_count)} of ${spd.leg_count}` : '—');
  text('spd-target', running ? spdTarget(spd.from ?? 0, spd.target ?? 0) : '—');
  text('spd-dir', running ? (spd.forward ? 'FORWARD' : 'BACK') : '—');
  text('spd-k', spdFixed(spd.stop_k, 3));
  let detail;
  if (phase === 'failed') detail = spdFailureDetail[spd.failure] || 'The run ended early.';
  else if (phase === 'finished') detail = 'All legs driven. The table and the CSV file are complete.';
  else if (phase === 'braking') detail = 'Braking before the next leg.';
  else if (phase === 'driving') detail = 'Driving. Press PANIC STOP if it heads for anything.';
  else detail = `Put the car on the start mark with ${spdFixed(spd.leg_m, 0)} m clear ahead, then press Start.`;
  if (running && !Number.isFinite(data.motor_temp_c)) detail += ' No motor temperature reading, so the overheat guard is off.';
  text('spd-detail', detail);
  $('spd-detail').className = 'measured-detail' + (phase === 'failed' ? ' hot' : '');
}
function render() {
  text('steering-slider-value', signed(Number($('steering').value)));
  text('speed-slider-value', signed(Number($('speed').value)));
  if (!state) return;
  const now = timestamp(), serverFresh = connected && performance.now() - received < 1800;
  const interval = state.config?.telemetry_interval_ms || 200;
  const fresh = serverFresh && state.car.fresh && now - state.car.receivedAt <= Math.max(2500, interval * 2.5);
  const data = state.telemetry || {}, command = state.command || {};
  const own = state.owner?.client === client;
  const armed = fresh && (command.control_state === 'armed' || data.control_state === 'armed');
  const latest = state.commandAt > state.car.receivedAt ? command : data;
  $('demo-banner').hidden = !state.demo; text('mode-chip', state.demo ? 'DEMO' : 'LIVE MQTT'); $('mode-chip').className = 'chip ' + (state.demo ? 'warn' : 'neutral');
  chip('broker-chip', serverFresh && state.broker.connected ? 'CONNECTED' : 'OFFLINE', serverFresh && state.broker.connected);
  $('broker-chip').title = state.broker.label;
  chip('car-chip', fresh ? 'RECEIVING' : state.car.online ? 'STALE' : 'OFFLINE', fresh, !fresh);
  text('vehicle-state', fresh ? (latest.control_state || 'unknown').toUpperCase() : 'NO LIVE DATA');
  $('vehicle-state').className = fresh && latest.control_state === 'armed' ? 'armed' : '';
  const timedOut = fresh && latest.reason === 'drive_timeout';
  text('motion-state', !fresh ? 'Waiting for fresh telemetry' : timedOut ? 'DRIVE TIMEOUT · no drive, still armed' : `${latest.motion_state || 'unknown'} · ${latest.reason || 'unknown'}`);
  text('motor-state', fresh ? motorLabels[data.motor?.state] || '—' : '—');
  $('motor-state').classList.toggle('alert', fresh && (data.motor?.state === 'braking' || timedOut));
  const duty = fresh ? Math.max(data.motor?.forward_duty || 0, data.motor?.backward_duty || 0) : null;
  text('duty', duty === null ? 'Duty —' : `Duty ${duty.toFixed(3)}`);
  const style = fresh ? data.drive_style : null;
  text('current-style', style ? styleLabels[style] || style : '—');
  text('style-detail', style === 'gap_calibration' ? 'Measures its own magnet gaps'
    : style === 'speed_calibration' ? 'Measures speed per duty'
    : style === 'manual_by_remote' ? 'Operator drives live' : 'Waiting for telemetry');
  // A style is chosen only while disarmed, which is what makes "arming starts the
  // selected style" answerable: one answer, fixed before anything can move.
  if (style && !styleTouched && $('style-select').value !== style) { $('style-select').value = style; }
  const canSelect = fresh && !!state.config && !armed && !state.owner && pending === 0;
  $('style-apply').disabled = !canSelect;
  $('style-select').disabled = !canSelect;
  $('style-lock').textContent = armed ? 'Stop the car to change it' : canSelect ? 'Select, then Start to run it' : 'Waiting for telemetry';
  renderCalibration(fresh, data, armed);
  renderSpeedCalibration(fresh, data);
  renderBattery(fresh, data);
  renderMotorTemp(fresh, data);
  renderOdometer(fresh, data);
  const seconds = Math.floor((data.uptime_ms || 0) / 1000);
  text('uptime', fresh ? `${String(Math.floor(seconds / 3600)).padStart(2, '0')}:${String(Math.floor(seconds / 60) % 60).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}` : '—');
  text('session', fresh ? command.session_id || '—' : '—');
  text('ownership', own ? 'This tab drives the car' : armed ? 'Another controller' : 'Monitoring only');
  text('heartbeat', own && state.owner.armed && serverFresh ? 'ACTIVE' : 'INACTIVE');
  text('heartbeat-detail', own && state.lastHeartbeatAt ? `Last sent ${Math.max(0, (now - state.lastHeartbeatAt) / 1000).toFixed(1)}s ago` : 'Sent only while controlling');
  // Echo: what the car applies, beside what the sliders ask for.
  const carSteering = fresh ? data.steering_deg : null, carSpeed = fresh ? data.motor?.speed_command : null;
  text('steering-car-value', signed(carSteering));
  text('speed-car-value', signed(carSpeed));
  $('steering-car-value').classList.toggle('differs', own && armed && carSteering !== null && Math.round(carSteering) !== Number($('steering').value));
  const askedSpeed = speedHeld ? 0 : Number($('speed').value);
  $('speed-car-value').classList.toggle('differs', own && armed && carSpeed !== null && Math.round(carSpeed) !== askedSpeed);
  renderPi(now, serverFresh, carSteering);
  remember(now); drawCharts(now);
  $('speed-hold').hidden = !(own && state.owner.armed && speedHeld && Number($('speed').value) !== 0);
  const ready = fresh && !!state.config && !!state.command;
  $('start').disabled = !ready || armed || !!state.owner || pending > 0;
  if (performance.now() > feedbackUntil) {
    text('feedback', !serverFresh ? 'Local console disconnected. Reconnecting; control will not restart automatically.' : state.notice);
    $('feedback').className = 'feedback';
  }
  text('stream-state', fresh ? state.demo ? 'SIMULATED DATA' : 'LIVE TELEMETRY' : 'WAITING FOR DATA');
  text('received-age', state.car.receivedAt ? `Last sample ${Math.max(0, (now - state.car.receivedAt) / 1000).toFixed(1)}s ago` : 'No samples yet');
}
function release() {
  if (state?.owner?.client === client && csrf) void post('release', {}, true).catch(() => {});
}
document.addEventListener('visibilitychange', () => { if (document.hidden) release(); });
window.addEventListener('pagehide', release);
setInterval(() => {
  if (state?.owner?.client === client && !document.hidden) void post('pulse').catch(() => { connected = false; });
}, 450);
// Stream the sliders even when they do not move: silence means the car stops driving.
setInterval(sendDrive, 100);
setInterval(render, 100);
$('battery-low-mark').style.left = batteryPercent(BATTERY_LOW_V) + '%';
$('battery-critical-mark').style.left = batteryPercent(BATTERY_CRITICAL_V) + '%';
async function connect() {
  try {
    const response = await fetch('/api/bootstrap'); if (!response.ok) throw Error('Local console unavailable.');
    const bootstrap = await response.json(); csrf = bootstrap.csrf; accept(bootstrap.state);
    stream?.close(); stream = new EventSource('/api/events');
    stream.onmessage = event => { try { accept(JSON.parse(event.data)); } catch { connected = false; } };
    stream.onerror = () => { connected = false; stream.close(); setTimeout(connect, 1500); };
  } catch { connected = false; message('Local console unavailable. Keep start-ui.ps1 running. Retrying…', 'error', 2000); setTimeout(connect, 2000); }
}
void connect();
