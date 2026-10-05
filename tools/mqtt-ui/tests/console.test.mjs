import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import http from 'node:http';
import { ConsoleControl, History, SUBSCRIPTIONS, TOPIC, piValues, validateConfig } from '../core.mjs';
import { segments, valueOf } from '../public/charts.mjs';
import { createConsole } from '../server.mjs';
import { DemoTransport, FordDemoTransport } from '../transport.mjs';

const config = { schema_version: 1, revision: 5, result: 'applied', stop_distance_cm: 30, drive_duty: .5, drive_style: 'decide_action', telemetry_interval_ms: 200 };
const telemetry = { schema_version: 1, sequence: 1, uptime_ms: 1000, control_state: 'disarmed', motion_state: 'stopped', distance_cm: { left: 42, center: 60, right: 35 }, motor: { speed_command: 0 }, steering_deg: 0 };
const command = { schema_version: 1, last_request_id: 8, session_id: '', control_state: 'disarmed', result: 'state' };
class Transport extends EventEmitter {
  label = 'test'; sent = []; callback = null;
  async publish(topic, data, qos, retain) { this.sent.push({ topic, data, qos, retain }); await this.callback?.(topic, data); }
}
function setup() {
  const transport = new Transport(); let time = 1800000000000;
  const control = new ConsoleControl(transport, { now: () => time, ackMs: 50 });
  const receive = (suffix, data, retain = false) => transport.emit('message', `${TOPIC}/${suffix}`, data, retain);
  transport.emit('connection', true);
  receive('status', { schema_version: 1, online: true }, true);
  receive('config/state', config, true); receive('command/state', command, true); receive('telemetry', telemetry);
  return { control, transport, receive, advance: ms => { time += ms; } };
}
function autoAck(fixture) {
  fixture.transport.callback = async (topic, data) => {
    if (data.command === 'start' || data.command === 'stop') fixture.receive('command/state', {
      ...command, last_request_id: data.request_id, result: 'accepted', control_state: data.command === 'start' ? 'armed' : 'disarmed', session_id: data.command === 'start' ? data.session_id : '',
    });
    if (topic.endsWith('/config/set')) fixture.receive('config/state', { ...data, result: 'applied' });
  };
}

test('system-test config validates reaction, stop and loop as one update', async () => {
  const f = setup(); autoAck(f);
  f.transport.callback = async (_, data) => f.receive('config/state', { ...data, system_test: true, result: 'applied' });
  f.receive('config/state', { ...config, system_test: true, reaction_distance_cm: 40, loop_interval_ms: 250 });
  await f.control.configure('owner', { stop_distance_cm: 20, drive_duty: 1, telemetry_interval_ms: 200, drive_style: 'decide_action', reaction_distance_cm: 45, loop_interval_ms: 20 });
  assert.equal(f.transport.sent.at(-1).data.reaction_distance_cm, 45);
  assert.equal(f.transport.sent.at(-1).data.loop_interval_ms, 20);
  const valid = { stop_distance_cm: 30, drive_duty: 0, telemetry_interval_ms: 1000, drive_style: 'decide_action', reaction_distance_cm: 40, loop_interval_ms: 250 };
  for (const changes of [{ reaction_distance_cm: 30 }, { reaction_distance_cm: NaN }, { loop_interval_ms: 20.5 }, { loop_interval_ms: 1001 }, { drive_style: 'slow_left' }]) {
    assert.throws(() => validateConfig({ ...valid, ...changes }, true));
  }
});

test('manual servo stops heartbeat and requires matching disarmed angle ACK', async () => {
  const f = setup(); autoAck(f); await f.control.start('owner');
  f.receive('config/state', { ...config, system_test: true, reaction_distance_cm: 40, loop_interval_ms: 250 });
  f.transport.callback = async (_, data) => {
    if (data.command === 'servo') f.receive('command/state', { ...command, last_request_id: data.request_id, result: 'accepted', servo_test: true, servo_angle_deg: data.angle_deg });
  };
  await f.control.servo('owner', -30);
  assert.equal(f.control.owner, null);
  assert.equal(f.transport.sent.at(-1).data.angle_deg, -30);
  assert.equal(f.transport.sent.at(-1).retain, false);
  const count = f.transport.sent.length; await f.control.tick(); assert.equal(f.transport.sent.length, count);
  for (const angle of [NaN, 91, -91, '30']) await assert.rejects(f.control.servo('owner', angle));
});

test('servo ACK without an angle is rejected and sends a stop', async () => {
  const f = setup();
  f.receive('config/state', { ...config, system_test: true, reaction_distance_cm: 40, loop_interval_ms: 250 });
  f.transport.callback = async (_, data) => {
    if (data.command === 'servo') f.receive('command/state', { ...command, last_request_id: data.request_id, result: 'accepted', servo_test: true });
  };
  await assert.rejects(f.control.servo('owner', 20), /not confirmed/);
  assert.equal(f.transport.sent.at(-1).data.command, 'stop');
});

test('a pending servo test blocks Start; Stop cancels the manual operation', async () => {
  const f = setup(); autoAck(f);
  f.receive('config/state', { ...config, system_test: true, reaction_distance_cm: 40, loop_interval_ms: 250 });
  const servo = f.control.servo('owner', 25); const rejected = assert.rejects(servo, /Stop requested/);
  await assert.rejects(f.control.start('owner'), /Already armed/);
  await f.control.stop(); await rejected;
  assert.equal(f.control.servoBusy, false);
  assert.equal(f.transport.sent.at(-1).data.command, 'stop');
});
test('retained state alone cannot start a car; opening and ticking never publishes', async () => {
  const f = setup(); await f.control.tick(); assert.equal(f.transport.sent.length, 0);
  f.transport.emit('connection', false); f.transport.emit('connection', true);
  f.receive('status', { schema_version: 1, online: true }, true); f.receive('telemetry', telemetry, true);
  await assert.rejects(f.control.start('a'), /fresh telemetry/); assert.equal(f.transport.sent.length, 0);
});
test('start needs a fresh matching car ACK; only the owning tab renews its lease', async () => {
  const f = setup(); autoAck(f); await f.control.start('owner');
  assert.equal(f.control.owner.armed, true); assert.equal(f.transport.sent[0].retain, false);
  await f.control.tick(); assert.equal(f.transport.sent.at(-1).data.command, 'heartbeat');
  f.advance(1700); f.control.pulse('another-tab'); await f.control.tick();
  assert.equal(f.control.owner, null); assert.equal(f.transport.sent.at(-1).data.command, 'stop');
  const count = f.transport.sent.length; f.advance(4000); await f.control.tick(); assert.equal(f.transport.sent.length, count);
});
test('replayed ACK is ignored and a start timeout sends a nonretained stop', async () => {
  const f = setup(); f.transport.callback = async (_, data) => {
    if (data.command === 'start') f.receive('command/state', { ...command, last_request_id: data.request_id, session_id: data.session_id, result: 'accepted', control_state: 'armed' }, true);
  };
  await assert.rejects(f.control.start('owner'), /acknowledgement/);
  assert.equal(f.control.owner, null); assert.equal(f.transport.sent.at(-1).data.command, 'stop');
  assert.ok(f.transport.sent.every(p => !p.retain));
});
test('wrong session ACK fails, without sending a heartbeat', async () => {
  const f = setup(); f.transport.callback = async (_, data) => {
    if (data.command === 'start') f.receive('command/state', { ...command, last_request_id: data.request_id, session_id: 'wrong', result: 'accepted', control_state: 'armed' });
  };
  await assert.rejects(f.control.start('owner'), /rejected/);
  assert.equal(f.transport.sent.some(p => p.data.command === 'heartbeat'), false);
});
test('stop supersedes pending start and checks the car acknowledgement', async () => {
  const f = setup(); f.transport.callback = async (_, data) => {
    if (data.command === 'stop') f.receive('command/state', { ...command, last_request_id: data.request_id, result: 'accepted' });
  };
  const start = f.control.start('owner'); const rejected = assert.rejects(start, /Stop requested/);
  await f.control.stop(); await rejected; assert.equal(f.control.owner, null);
  assert.ok(f.transport.sent[1].data.request_id > f.transport.sent[0].data.request_id);
});
test('broker disconnect cancels control and reconnect never starts automatically', async () => {
  const f = setup(); autoAck(f); await f.control.start('owner');
  f.transport.emit('connection', false); assert.equal(f.control.owner, null); assert.equal(f.control.fresh(), false);
  const count = f.transport.sent.length; f.transport.emit('connection', true); await f.control.tick(); assert.equal(f.transport.sent.length, count);
});
test('stale telemetry stops even if the browser keeps its lease', async () => {
  const f = setup(); autoAck(f); await f.control.start('owner'); f.advance(2600); f.control.pulse('owner'); await f.control.tick();
  assert.equal(f.control.owner, null); assert.equal(f.transport.sent.at(-1).data.command, 'stop');
});
test('configuration boundaries, full duty and newer observed revisions', async () => {
  const f = setup(); autoAck(f);
  f.receive('config/state', { ...config, revision: 2000000000 }, true);
  await f.control.configure('owner', { stop_distance_cm: 70, drive_duty: 1, telemetry_interval_ms: 200, drive_style: 'gradual_sweep' });
  assert.equal(f.transport.sent[0].data.revision, 2000000001); assert.equal(f.transport.sent[0].retain, true);
  for (const drive_duty of [-.1, 1.01, NaN, '0.5']) assert.throws(() => validateConfig({ stop_distance_cm: 30, drive_duty, telemetry_interval_ms: 1000, drive_style: 'decide_action' }));
  assert.throws(() => validateConfig({ stop_distance_cm: 20, drive_duty: 0, telemetry_interval_ms: 1000, drive_style: 'decide_action' }));
  assert.throws(() => validateConfig({ stop_distance_cm: 30, drive_duty: 0, telemetry_interval_ms: 100, drive_style: 'decide_action' }));
});
test('changing style while armed is blocked; zero duty is allowed; other tab cannot configure', async () => {
  const f = setup(); autoAck(f); await f.control.start('owner');
  const input = { stop_distance_cm: 35, drive_duty: 0, telemetry_interval_ms: 200, drive_style: 'decide_action' };
  await assert.rejects(f.control.configure('owner', { ...input, drive_style: 'slow_left' }), /Stop the car/);
  await assert.rejects(f.control.configure('other', input), /Another browser/);
  await f.control.configure('owner', input); assert.equal(f.transport.sent.at(-1).data.drive_duty, 0);
});
test('rejected configuration is surfaced, not reported as success', async () => {
  const f = setup(); f.transport.callback = async (_, data) => f.receive('config/state', { ...config, revision: data.revision, result: 'rejected', error: 'stale_revision' });
  await assert.rejects(f.control.configure('a', { stop_distance_cm: 30, drive_duty: .5, telemetry_interval_ms: 200, drive_style: 'decide_action' }), /stale_revision/);
});
test('history rolls without new data, drops duplicates, and clears on device reboot', () => {
  const history = new History(); history.ingest(telemetry, 1000);
  assert.equal(history.ingest(telemetry, 1500), false);
  history.ingest({ ...telemetry, sequence: 2, uptime_ms: 2000 }, 2000);
  history.prune(11500); assert.equal(history.samples.length, 1);
  history.ingest({ ...telemetry, sequence: 1, uptime_ms: 100 }, 12000); assert.equal(history.samples.length, 1);
  history.prune(22001); assert.equal(history.samples.length, 0);
});
test('graph gaps preserve invalid samples and outages instead of inventing zeroes', () => {
  const samples = [0, 200, 400, 600, 2000].map((at, i) => ({ at, data: { ...telemetry, distance_cm: { left: i === 2 ? null : 42 } } }));
  assert.equal(valueOf(samples[2].data, 'left'), null);
  assert.equal(valueOf({ distance_cm: { left: '42' } }, 'left'), null);
  assert.deepEqual(segments(samples, 'left', 2100, 200).map(g => g.length), [2, 1, 1]);
  assert.equal(segments(samples, 'left', 13000, 200).length, 0);
});
test('HTTP is loopback-only, serves only allowlisted files and requires origin + control token', async t => {
  const transport = new DemoTransport(), app = createConsole(transport, 0), url = await app.listen(); t.after(() => app.close());
  const boot = await (await fetch(url + '/api/bootstrap')).json();
  assert.equal(boot.state.demo, true); assert.equal(boot.state.owner, null);
  assert.equal(JSON.stringify(boot).includes('password'), false);
  for (const resource of ['/core.mjs', '/.env', '/..%2fmqtt%2f.env']) assert.equal((await fetch(url + resource)).status, 404);
  const body = JSON.stringify({ client: 'a'.repeat(32) });
  assert.equal((await fetch(url + '/api/start', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body })).status, 403);
  assert.equal((await fetch(url + '/api/start', { method: 'POST', headers: { Origin: 'https://foreign.invalid', 'Content-Type': 'application/json', 'X-Cnb-Token': boot.csrf }, body })).status, 403);
  const hostStatus = await new Promise(resolve => {
    http.get(url + '/api/bootstrap', { headers: { Host: 'foreign.invalid' } }, response => { response.resume(); resolve(response.statusCode); });
  });
  assert.equal(hostStatus, 403);
  const options = { method: 'POST', headers: { Origin: url, 'Content-Type': 'application/json', 'X-Cnb-Token': boot.csrf }, body };
  assert.equal((await fetch(url + '/api/start', options)).status, 200);
  assert.equal((await fetch(url + '/api/stop', options)).status, 200);
  assert.equal(app.control.owner, null);
});
test('LAN access is off by default and can only be toggled from 127.0.0.1', async t => {
  const transport = new DemoTransport(), app = createConsole(transport, 0, 'ford', { addresses: () => ['127.0.0.2'] }), url = await app.listen(); t.after(() => app.close());
  const port = new URL(url).port, lan = `http://127.0.0.2:${port}`;
  assert.deepEqual(await (await fetch(url + '/api/lan')).json(), { enabled: false, urls: [], local: true });
  await assert.rejects(fetch(lan + '/api/bootstrap'));
  const { csrf } = await (await fetch(url + '/api/bootstrap')).json();
  const toggle = (base, enabled) => fetch(base + '/api/lan', { method: 'POST', headers: { Origin: base, 'Content-Type': 'application/json', 'X-Cnb-Token': csrf }, body: JSON.stringify({ client: 'a'.repeat(32), enabled }) });
  const on = await toggle(url, true);
  assert.equal(on.status, 200); assert.deepEqual((await on.json()).urls, [lan]);
  assert.equal((await fetch(lan + '/')).status, 200);
  assert.equal((await (await fetch(lan + '/api/lan')).json()).local, false);
  assert.equal((await toggle(lan, false)).status, 403);
  const spoofed = await new Promise(resolve => {
    http.get(lan + '/api/lan', { headers: { Host: `127.0.0.1:${port}` } }, response => { response.resume(); resolve(response.statusCode); });
  });
  assert.equal(spoofed, 403);
  assert.equal((await toggle(url, false)).status, 200);
  await assert.rejects(fetch(lan + '/api/bootstrap'));
});

const pi = { schema_version: 1, measured_speed_mps: { slam: 0.42 }, wheel_angle_deg: { slam: -12.3 }, slam_state: 'tracking', cpu_temp_c: 61.2 };
function fordSetup() {
  const transport = new Transport(); let time = 1800000000000;
  const control = new ConsoleControl(transport, { now: () => time, ackMs: 50, car: 'ford' });
  const receive = (suffix, data, retain = false) => transport.emit('message', `cnb/ford/${suffix}`, data, retain);
  transport.emit('connection', true);
  return { control, receive, advance: ms => { time += ms; } };
}

test('Ford console keeps Pi telemetry fresh for 2.5 intervals', () => {
  const f = fordSetup();
  f.receive('pi/status', { schema_version: 1, online: true }, true);
  f.receive('pi/telemetry', pi);
  let snapshot = f.control.snapshot().pi;
  assert.equal(snapshot.fresh, true);
  assert.deepEqual(snapshot.telemetry, { measured_speed_mps: { slam: 0.42 }, wheel_angle_deg: { slam: -12.3 }, slam_state: 'tracking', cpu_temp_c: 61.2 });
  assert.equal(snapshot.history.length, 1);
  f.advance(500); assert.equal(f.control.snapshot().pi.fresh, true);
  f.advance(1); assert.equal(f.control.snapshot().pi.fresh, false);
  f.receive('pi/status', { schema_version: 1, online: false }, true);
  snapshot = f.control.snapshot().pi;
  assert.equal(snapshot.online, false); assert.equal(snapshot.telemetry, null);
});

test('Ford console ignores retained, malformed and wrong-version Pi telemetry', () => {
  const f = fordSetup();
  f.receive('pi/status', { schema_version: 1, online: true }, true);
  f.receive('pi/telemetry', pi, true);
  f.receive('pi/telemetry', { ...pi, slam_state: 'flying' });
  f.receive('pi/telemetry', { ...pi, schema_version: 2 });
  assert.equal(f.control.snapshot().pi.telemetry, null);
});

test('Pi values keep each source and turn unknowns into null', () => {
  const values = piValues({ ...pi, measured_speed_mps: { slam: null, odometer_left_rear: 0.5, 'Bad Key': 1 }, wheel_angle_deg: [1], cpu_temp_c: '61' });
  assert.deepEqual(values.measured_speed_mps, { slam: null, odometer_left_rear: 0.5 });
  assert.deepEqual(values.wheel_angle_deg, {});
  assert.equal(values.cpu_temp_c, null);
  assert.equal(piValues({ ...pi, slam_state: undefined }), null);
});

test('only the Ford subscribes to and reports Pi topics', () => {
  assert.ok(SUBSCRIPTIONS.ford.includes('pi/telemetry') && SUBSCRIPTIONS.ford.includes('pi/status'));
  assert.ok(!SUBSCRIPTIONS.vagrant.some(t => t.startsWith('pi/')));
  const f = setup();
  f.receive('pi/telemetry', pi);
  assert.equal(f.control.snapshot().pi, undefined);
});

test('Ford demo simulates the Pi with the agreed payload', async () => {
  const demo = new FordDemoTransport(), seen = [];
  demo.on('message', (topic, data, retained) => seen.push({ topic, data, retained }));
  demo.start(); await new Promise(resolve => setTimeout(resolve, 300)); demo.close();
  assert.ok(seen.some(m => m.topic === 'cnb/ford/pi/status' && m.data.online === true && m.retained));
  const sample = seen.find(m => m.topic === 'cnb/ford/pi/telemetry');
  assert.ok(sample);
  assert.deepEqual(Object.keys(sample.data).sort(), ['cpu_temp_c', 'measured_speed_mps', 'schema_version', 'slam_state', 'wheel_angle_deg']);
  assert.ok(piValues(sample.data));
});

test('Ford demo reports a drive battery voltage', async () => {
  const demo = new FordDemoTransport(), seen = [];
  demo.on('message', (topic, data) => seen.push({ topic, data }));
  demo.start(); await new Promise(resolve => setTimeout(resolve, 300)); demo.close();
  const sample = seen.find(m => m.topic === 'cnb/ford/telemetry');
  assert.ok(sample);
  assert.ok(Number.isFinite(sample.data.battery_v) && sample.data.battery_v >= 5 && sample.data.battery_v <= 9);
});

test('Ford demo reports a motor temperature', async () => {
  const demo = new FordDemoTransport(), seen = [];
  demo.on('message', (topic, data) => seen.push({ topic, data }));
  demo.start(); await new Promise(resolve => setTimeout(resolve, 300)); demo.close();
  const sample = seen.find(m => m.topic === 'cnb/ford/telemetry');
  assert.ok(sample);
  assert.ok(Number.isFinite(sample.data.motor_temp_c) && sample.data.motor_temp_c >= 20 && sample.data.motor_temp_c <= 60);
});

test('Ford demo reports the odometer', async () => {
  const demo = new FordDemoTransport(), seen = [];
  demo.on('message', (topic, data) => seen.push({ topic, data }));
  demo.start(); await new Promise(resolve => setTimeout(resolve, 300)); demo.close();
  const sample = seen.find(m => m.topic === 'cnb/ford/telemetry');
  assert.ok(sample);
  assert.equal(sample.data.measured_speed_ms, 0);
  assert.equal(sample.data.measured_speed_source, 'none');
  assert.ok(Number.isFinite(sample.data.odometer_distance_m) && sample.data.odometer_distance_m >= 0);
  assert.ok(Number.isInteger(sample.data.odometer_phase_losses));
});

// Ford's two drive styles. Its config/set is narrowed to the style, and a measured gap
// table is written only when the operator confirms it. See ADR 0009.
function fordStyleSetup(style = 'manual_by_remote', calibration = undefined) {
  const transport = new Transport(); let time = 1800000000000;
  const control = new ConsoleControl(transport, { now: () => time, ackMs: 50, car: 'ford' });
  const receive = (suffix, data, retain = false) => transport.emit('message', `cnb/ford/${suffix}`, data, retain);
  transport.emit('connection', true);
  receive('status', { schema_version: 1, online: true }, true);
  receive('config/state', { schema_version: 1, revision: 4, drive_style: style }, true);
  receive('command/state', { ...command, control_state: 'disarmed' }, true);
  receive('telemetry', { ...telemetry, drive_style: style, control_state: 'disarmed', ...(calibration ? { calibration } : {}) });
  transport.callback = async (topic, data) => {
    if (topic.endsWith('/config/set')) receive('config/state', { ...data, result: 'applied' });
    if (data.command === 'store_gaps') receive('command/state', { ...command, last_request_id: data.request_id, result: 'accepted', control_state: 'disarmed' });
  };
  return { control, transport, receive, advance: ms => { time += ms; } };
}

test('Ford accepts a gap_calibration config/state instead of dropping it', () => {
  const f = fordStyleSetup('gap_calibration');
  assert.equal(f.control.snapshot().config.drive_style, 'gap_calibration');
  // An unknown style is still refused, so a stray payload cannot select one.
  f.receive('config/state', { schema_version: 1, revision: 5, drive_style: 'decide_action' }, true);
  assert.equal(f.control.snapshot().config.revision, 4);
});

test('Ford drive style selection sends only the three narrowed fields', async () => {
  const f = fordStyleSetup();
  await f.control.selectDriveStyle('tab', 'gap_calibration');
  const sent = f.transport.sent.filter(m => m.topic.endsWith('/config/set'));
  assert.equal(sent.length, 1);
  assert.deepEqual(Object.keys(sent[0].data).sort(), ['drive_style', 'revision', 'schema_version']);
  assert.equal(sent[0].data.drive_style, 'gap_calibration');
  assert.equal(sent[0].retain, true);
  await assert.rejects(f.control.selectDriveStyle('tab', 'decide_action'), /Unknown Ford drive style/);
});

test('a measured gap table is stored only once the operator confirms it', async () => {
  // Nothing measured yet: there is nothing to confirm.
  const idle = fordStyleSetup('gap_calibration', { phase: 'sampling', duty_index: 1, duty_count: 3, revolutions: 7, revolutions_wanted: 20 });
  await assert.rejects(idle.control.storeGaps(), /No measured gap table/);

  // A disagreeing run produced no table, so it cannot be confirmed either.
  const refused = fordStyleSetup('gap_calibration', { phase: 'failed', failure: 'disagreed', spread: 0.043, duty_count: 3 });
  await assert.rejects(refused.control.storeGaps(), /No measured gap table/);

  const measured = fordStyleSetup('gap_calibration', {
    phase: 'measured', duty_count: 3, revolutions: 20, revolutions_wanted: 20,
    gaps: [0.125, 0.125, 0.25, 0.25, 0.125, 0.125], spread: 0.0012, phase_margin: 0.24, stored: false,
  });
  await measured.control.storeGaps();
  const sent = measured.transport.sent.filter(m => m.data.command === 'store_gaps');
  assert.equal(sent.length, 1);
  assert.equal(sent[0].topic, 'cnb/ford/command');
  assert.match(measured.control.snapshot().notice, /stored/i);
});

test('during gap calibration the console sends heartbeats, not refused drive commands', async () => {
  // The car refuses drive commands outside ManualByRemote and a refused one renews no
  // lease, so streaming them would starve the heartbeat and time out the run at 3 s.
  for (const style of ['gap_calibration', 'manual_by_remote']) {
    const f = fordStyleSetup(style);
    const streamed = [];
    f.transport.stream = async (topic, data) => { streamed.push(data); };
    const ack = f.transport.callback;
    f.transport.callback = async (topic, data) => {
      await ack(topic, data);
      if (data.command === 'start') f.receive('command/state', { ...command, last_request_id: data.request_id, result: 'accepted', control_state: 'armed', session_id: data.session_id });
    };
    await f.control.start('owner');
    const sent = await f.control.drive('owner', 0, 0);
    f.advance(1000); await f.control.tick();
    const heartbeat = f.transport.sent.at(-1).data.command === 'heartbeat';
    if (style === 'gap_calibration') {
      assert.equal(sent, false); assert.equal(streamed.length, 0); assert.equal(heartbeat, true);
    } else {
      assert.equal(sent, true); assert.equal(streamed[0].command, 'drive');
    }
  }
});

test('the gap calibration style is not offered on the Vagrant console', async () => {
  const f = setup();
  await assert.rejects(f.control.selectDriveStyle('tab', 'gap_calibration'), /for the Ford only/);
  await assert.rejects(f.control.storeGaps(), /for the Ford only/);
});
