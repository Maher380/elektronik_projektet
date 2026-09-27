import { Chart, valueOf } from './charts.mjs';
const $ = id => document.getElementById(id);
const client = crypto.randomUUID();
const charts = ['left', 'center', 'right', 'steering', 'speed'].map(key => new Chart(key));
const styles = { decide_action: 'DecideAction', slow_left: 'SlowLeft', slow_right: 'SlowRight', gradual_sweep: 'GradualSweep' };
let state = null, csrf = null, stream = null, connected = false, received = 0, dirty = false, pending = 0, configLoaded = false;
let feedbackUntil = 0;
const text = (id, value) => { $(id).textContent = value; };
const number = (n, digits = 1) => typeof n === 'number' && Number.isFinite(n) ? n.toFixed(digits) : '—';
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
async function act(action, extra = {}) {
  pending++; message('Waiting for acknowledgement from the car…', '', 6000);
  try { await post(action, extra); message(state.notice, 'success'); return true; }
  catch (error) { message(error.message + (action === 'config' ? ' Check the confirmed settings below before retrying.' : ''), 'error', 12000); return false; }
  finally { pending--; }
}
$('start').addEventListener('click', () => void act('start'));
$('stop').addEventListener('click', () => void act('stop'));
$('configuration').addEventListener('input', () => { dirty = true; });
$('configuration').addEventListener('submit', async event => {
  event.preventDefault();
  const config = { driver_style: $('driver-style').value, drive_duty: Number($('drive-duty').value), stop_distance_cm: Number($('stop-distance').value), telemetry_interval_ms: Number($('telemetry-interval').value) };
  if (await act('config', { config })) dirty = false;
});
function loadConfig(config) {
  $('driver-style').value = config.driver_style;
  $('drive-duty').value = config.drive_duty;
  $('stop-distance').value = config.stop_distance_cm;
  const interval = $('telemetry-interval');
  if (![...interval.options].some(o => Number(o.value) === config.telemetry_interval_ms)) {
    const option = document.createElement('option'); option.value = config.telemetry_interval_ms; option.textContent = `${config.telemetry_interval_ms} ms`; interval.append(option);
  }
  interval.value = config.telemetry_interval_ms; configLoaded = true;
}
function chip(id, label, good, warning = false) {
  const item = $(id); item.className = 'chip ' + (good ? 'good' : warning ? 'warn' : 'neutral'); item.querySelector('strong').textContent = label;
}
function render() {
  if (!state) return;
  const now = timestamp(), serverFresh = connected && performance.now() - received < 1800;
  const interval = state.config?.telemetry_interval_ms || 1000;
  const fresh = serverFresh && state.car.fresh && now - state.car.receivedAt <= Math.max(2500, interval * 2.5);
  const data = state.telemetry || {}, command = state.command || {}, config = state.config;
  const own = state.owner?.client === client;
  const armed = fresh && (command.control_state === 'armed' || data.control_state === 'armed');
  const latestState = state.commandAt > state.car.receivedAt ? command : data;
  const displayedControl = latestState.control_state;
  $('demo-banner').hidden = !state.demo; text('mode-chip', state.demo ? 'DEMO' : 'LIVE MQTT'); $('mode-chip').className = 'chip ' + (state.demo ? 'warn' : 'neutral');
  chip('broker-chip', serverFresh && state.broker.connected ? 'CONNECTED' : 'OFFLINE', serverFresh && state.broker.connected);
  $('broker-chip').title = state.broker.label;
  chip('car-chip', fresh ? 'RECEIVING' : state.car.online ? 'STALE' : 'OFFLINE', fresh, !fresh);
  text('vehicle-state', fresh ? (displayedControl || 'unknown').toUpperCase() : 'NO LIVE DATA');
  $('vehicle-state').className = fresh && displayedControl === 'armed' ? 'armed' : '';
  text('motion-state', fresh ? `${latestState.motion_state || 'unknown'} · ${latestState.reason || 'unknown'}` : 'Waiting for fresh telemetry');
  text('current-style', fresh ? styles[data.driver_style] || data.driver_style || '—' : '—');
  const seconds = Math.floor((data.uptime_ms || 0) / 1000);
  text('uptime', fresh ? `${String(Math.floor(seconds / 3600)).padStart(2, '0')}:${String(Math.floor(seconds / 60) % 60).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}` : '—');
  text('sequence', fresh ? '#' + data.sequence : '—');
  const samples = state.history.filter(p => now - p.at <= 10000);
  const span = samples.length > 1 ? samples.at(-1).at - samples[0].at : 0;
  text('sample-rate', fresh && span > 0 ? `${((samples.length - 1) * 1000 / span).toFixed(1)} Hz received` : 'Waiting for samples');
  text('session', fresh ? command.session_id || '—' : '—');
  text('ownership', own ? 'This tab controls the car' : armed ? 'Another controller' : 'Monitoring only');
  text('heartbeat', own && state.owner.armed && serverFresh ? 'ACTIVE' : 'INACTIVE');
  text('heartbeat-detail', own && state.lastHeartbeatAt ? `Last sent ${Math.max(0, (now - state.lastHeartbeatAt) / 1000).toFixed(1)}s ago` : 'Sent only while controlling');
  for (const key of ['left', 'center', 'right']) {
    const value = fresh ? valueOf(data, key) : null;
    text(key + '-value', number(value)); text(key + '-adc', 'ADC ' + (fresh ? number(data.adc_raw?.[key], 0) : '—'));
    const badge = $(key + '-badge');
    const outside = value !== null && (value < 10 || value > 80);
    const near = value !== null && config && value < config.stop_distance_cm;
    badge.textContent = !fresh ? 'NO LIVE DATA' : value === null ? 'INVALID' : outside ? 'OUT OF RANGE' : near ? 'BELOW LIMIT' : 'IN RANGE';
    badge.title = 'Sharp GP2Y0A21: nominal measuring range 10–80 cm. Below limit compares this sensor with the configured stop distance; the car may choose another direction.';
    badge.classList.toggle('alert', value === null || outside || near);
    text(key + '-range', 'AUTO SCALE · CM');
  }
  text('steering-value', fresh ? number(valueOf(data, 'steering')) : '—');
  text('speed-value', fresh ? number(valueOf(data, 'speed'), 2) : '—');
  text('pwm', `PWM F ${fresh ? number(data.motor?.forward_duty, 2) : '—'} / B ${fresh ? number(data.motor?.backward_duty, 2) : '—'}`);
  document.querySelectorAll('.chart-card').forEach(card => card.classList.toggle('stale', !fresh));
  for (const chart of charts) chart.draw(state.history, now, interval, config?.stop_distance_cm, !fresh);
  if (config && (!configLoaded || !dirty) && !pending) loadConfig(config);
  const ready = fresh && !!config && !!state.command;
  $('start').disabled = !ready || armed || !!state.owner || pending > 0;
  $('stop').disabled = !(serverFresh && state.broker.connected);
  $('apply').disabled = !ready || pending > 0 || (!!state.owner && !own);
  $('driver-style').disabled = !ready || armed || pending > 0;
  for (const id of ['drive-duty', 'stop-distance', 'telemetry-interval']) $(id).disabled = !ready || pending > 0 || (!!state.owner && !own);
  text('config-status', config ? `${dirty ? 'UNSENT CHANGES · ' : ''}Confirmed: ${styles[config.driver_style]} · duty ${number(config.drive_duty, 2)} · stop ${number(config.stop_distance_cm, 0)} cm · ${config.telemetry_interval_ms} ms` : 'Waiting for configuration from car');
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
setInterval(render, 100);
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
