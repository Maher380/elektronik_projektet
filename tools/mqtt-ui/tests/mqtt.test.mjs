import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import net from 'node:net';
import { existsSync } from 'node:fs';
import { once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';
import { MosquittoTransport, executable } from '../transport.mjs';
import { ConsoleControl, TOPIC } from '../core.mjs';

async function until(check, label, timeout = 5000) {
  const deadline = Date.now() + timeout;
  while (!check()) { if (Date.now() > deadline) throw Error('Timed out: ' + label); await delay(25); }
}
test('isolated real Mosquitto: subscribe, retained state, car ACK, config, heartbeat, stop, outage', { timeout: 20000 }, async t => {
  const brokerExe = executable('mosquitto');
  if (!existsSync(brokerExe)) { t.skip('Mosquitto executable not installed in the default directory.'); return; }
  const reservation = net.createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
  const port = reservation.address().port; await new Promise(resolve => reservation.close(resolve));
  // No .env, no real broker, no physical car. Mosquitto without a config binds loopback only.
  const broker = spawn(brokerExe, ['-p', String(port), '-v'], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  let brokerReady = false; const log = data => { if (String(data).includes('running')) brokerReady = true; };
  broker.stdout.on('data', log); broker.stderr.on('data', log);
  t.after(() => broker.kill()); await until(() => brokerReady, 'test broker ready');
  const settings = { host: '127.0.0.1', port, username: 'isolated-test', password: 'not-a-real-secret' };
  const transport = new MosquittoTransport(settings), control = new ConsoleControl(transport);
  t.after(() => { control.cancel('test finished'); transport.close(); });
  const cfg = { schema_version: 1, revision: 0, result: 'defaults', stop_distance_cm: 30, drive_duty: .5, telemetry_interval_ms: 200, driver_style: 'decide_action' };
  const state = { schema_version: 1, last_request_id: 0, session_id: '', result: 'state', control_state: 'disarmed', motion_state: 'stopped', reason: 'boot', driver_style: 'decide_action' };
  const commands = [];
  const carSub = transport.child('mosquitto_sub', ['-t', TOPIC + '/command', '-t', TOPIC + '/config/set', '-q', '1', '-F', '%j', '-d']);
  let carReady = false, buffer = '', errors = [];
  carSub.stderr.on('data', chunk => { if (String(chunk).includes('SUBACK')) carReady = true; });
  carSub.stdout.setEncoding('utf8'); carSub.stdout.on('data', chunk => {
    buffer += chunk;
    let newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
      const line = buffer.slice(0, newline).trim(); buffer = buffer.slice(newline + 1);
      if (line.includes('SUBACK')) carReady = true;
      if (!line.startsWith('{')) continue;
      void (async () => {
        const envelope = JSON.parse(line), data = JSON.parse(envelope.payload);
        if (envelope.topic.endsWith('/config/set')) {
          Object.assign(cfg, data, { result: 'applied' });
          await transport.publish(TOPIC + '/config/state', cfg, 1, true);
        } else {
          commands.push(data);
          assert.equal(Boolean(envelope.retain), false);
          if (data.command === 'heartbeat') return;
          Object.assign(state, { last_request_id: data.request_id, result: 'accepted', control_state: data.command === 'start' ? 'armed' : 'disarmed', session_id: data.command === 'start' ? data.session_id : '' });
          await transport.publish(TOPIC + '/command/state', state, 1, true);
        }
      })().catch(error => errors.push(error));
    }
  });
  t.after(() => carSub.kill());
  transport.start(); await until(() => control.connected, 'subscriptions acknowledged');
  // A retained test configuration flushes the fake car's buffered debug output.
  await transport.publish(TOPIC + '/config/set', { ...cfg, revision: 1 }, 1, true);
  await until(() => carReady, 'fake car subscription');
  await transport.publish(TOPIC + '/status', { schema_version: 1, online: true }, 1, true);
  await transport.publish(TOPIC + '/config/state', cfg, 1, true);
  await transport.publish(TOPIC + '/command/state', state, 1, true);
  let sequence = 0;
  const telemetry = () => transport.publish(TOPIC + '/telemetry', { ...state, sequence: ++sequence, uptime_ms: sequence * 200, distance_cm: { left: 45, center: 65, right: 35 }, steering_deg: 0, motor: { speed_command: state.control_state === 'armed' ? cfg.drive_duty : 0 } }, 0, false);
  await telemetry(); await until(() => control.fresh() && control.config && control.command, 'fresh car data');
  assert.equal(commands.length, 0, 'monitoring must never send commands');
  await control.configure('test-tab', { stop_distance_cm: 35, drive_duty: 1, telemetry_interval_ms: 200, driver_style: 'gradual_sweep' });
  assert.equal(control.config.drive_duty, 1);
  await control.start('test-tab'); assert.equal(control.owner.armed, true);
  await control.tick(); await until(() => commands.some(c => c.command === 'heartbeat'), 'heartbeat');
  await telemetry(); await control.stop(); assert.equal(control.command.control_state, 'disarmed');
  assert.equal(commands.filter(c => c.command === 'start').length, 1);
  assert.deepEqual(errors, []);
  broker.kill(); await until(() => !control.connected, 'broker disconnect', 17000);
  assert.equal(control.owner, null);
});
