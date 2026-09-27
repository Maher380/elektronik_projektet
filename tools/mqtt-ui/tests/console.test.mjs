import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import http from 'node:http';
import { ConsoleControl, History, TOPIC, validateConfig } from '../core.mjs';
import { segments, valueOf } from '../public/charts.mjs';
import { createConsole } from '../server.mjs';
import { DemoTransport } from '../transport.mjs';

const config = { schema_version: 1, revision: 5, result: 'applied', stop_distance_cm: 30, drive_duty: .5, driver_style: 'decide_action', telemetry_interval_ms: 200 };
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
  await f.control.configure('owner', { stop_distance_cm: 70, drive_duty: 1, telemetry_interval_ms: 200, driver_style: 'gradual_sweep' });
  assert.equal(f.transport.sent[0].data.revision, 2000000001); assert.equal(f.transport.sent[0].retain, true);
  for (const drive_duty of [-.1, 1.01, NaN, '0.5']) assert.throws(() => validateConfig({ stop_distance_cm: 30, drive_duty, telemetry_interval_ms: 1000, driver_style: 'decide_action' }));
  assert.throws(() => validateConfig({ stop_distance_cm: 20, drive_duty: 0, telemetry_interval_ms: 1000, driver_style: 'decide_action' }));
  assert.throws(() => validateConfig({ stop_distance_cm: 30, drive_duty: 0, telemetry_interval_ms: 100, driver_style: 'decide_action' }));
});
test('changing style while armed is blocked; zero duty is allowed; other tab cannot configure', async () => {
  const f = setup(); autoAck(f); await f.control.start('owner');
  const input = { stop_distance_cm: 35, drive_duty: 0, telemetry_interval_ms: 200, driver_style: 'decide_action' };
  await assert.rejects(f.control.configure('owner', { ...input, driver_style: 'slow_left' }), /Stop the car/);
  await assert.rejects(f.control.configure('other', input), /Another browser/);
  await f.control.configure('owner', input); assert.equal(f.transport.sent.at(-1).data.drive_duty, 0);
});
test('rejected configuration is surfaced, not reported as success', async () => {
  const f = setup(); f.transport.callback = async (_, data) => f.receive('config/state', { ...config, revision: data.revision, result: 'rejected', error: 'stale_revision' });
  await assert.rejects(f.control.configure('a', { stop_distance_cm: 30, drive_duty: .5, telemetry_interval_ms: 200, driver_style: 'decide_action' }), /stale_revision/);
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
