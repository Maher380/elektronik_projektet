import { EventEmitter } from 'node:events';
import { spawn } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { TOPIC, clock } from './core.mjs';

export function readSettings(file) {
  const values = {};
  for (const line of readFileSync(file, 'utf8').replace(/^\uFEFF/, '').split(/\r?\n/)) {
    if (!line.trim() || line.trim().startsWith('#')) continue;
    const separator = line.indexOf('=');
    if (separator < 1) throw Error('Invalid line in tools/mqtt/.env.');
    values[line.slice(0, separator).trim()] = line.slice(separator + 1).trim();
  }
  for (const name of ['HOST', 'PORT', 'USERNAME', 'PASSWORD']) {
    if (!values['CNB_MQTT_' + name] || values['CNB_MQTT_' + name].includes('REPLACE_')) throw Error(`Set CNB_MQTT_${name} in tools/mqtt/.env.`);
  }
  const port = Number(values.CNB_MQTT_PORT);
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw Error('Invalid MQTT port.');
  return { host: values.CNB_MQTT_HOST, port, username: values.CNB_MQTT_USERNAME, password: values.CNB_MQTT_PASSWORD };
}

export function executable(name) {
  const windows = path.join(process.env.ProgramFiles || 'C:/Program Files', 'mosquitto', name + '.exe');
  return existsSync(windows) ? windows : name;
}

// The installed Mosquitto clients handle MQTT, TCP, QoS and authentication.
// No shell interpolation, no credentials served to the browser, no raw errors logged.
export class MosquittoTransport extends EventEmitter {
  constructor(settings) {
    super(); this.settings = settings; this.demo = false;
    this.label = `${settings.host}:${settings.port}`; this.closed = false;
    this.children = new Set(); this.retry = null; this.connected = false;
  }
  args() {
    const s = this.settings;
    return ['-h', s.host, '-p', String(s.port), '-u', s.username, '-P', s.password, '-V', 'mqttv311'];
  }
  child(name, extra) {
    const child = spawn(executable(name), [...this.args(), ...extra], { windowsHide: true, shell: false, stdio: ['pipe', 'pipe', 'pipe'] });
    this.children.add(child); child.once('close', () => this.children.delete(child));
    return child;
  }
  setConnected(value) { if (this.connected !== value) { this.connected = value; this.emit('connection', value); } }
  start() {
    if (this.closed) return;
    if (!this.probeTimer) {
      void this.probe();
      this.probeTimer = setInterval(() => void this.probe(), 4000);
    }
    const subscriptions = ['telemetry', 'status', 'config/state', 'command/state'].flatMap(t => ['-t', `${TOPIC}/${t}`]);
    const child = this.child('mosquitto_sub', [...subscriptions, '-q', '1', '-k', '5', '-F', '%j', '-d']);
    const line = text => {
      if (!text.startsWith('{')) return;
      try {
        const message = JSON.parse(text);
        if (typeof message.topic !== 'string' || typeof message.payload !== 'string' || message.payload.length > 8192) return;
        this.setConnected(true);
        this.emit('message', message.topic, JSON.parse(message.payload), Boolean(message.retain));
      } catch { /* Malformed packets do not replace valid telemetry. */ }
    };
    for (const stream of [child.stdout, child.stderr]) {
      let buffer = '';
      stream.setEncoding('utf8');
      stream.on('data', chunk => {
        buffer += chunk;
        if (buffer.length > 65536) { buffer = ''; child.kill(); return; }
        let newline;
        while ((newline = buffer.indexOf('\n')) >= 0) { line(buffer.slice(0, newline).trim()); buffer = buffer.slice(newline + 1); }
      });
    }
    child.once('error', () => this.emit('problem'));
    child.once('close', () => {
      this.setConnected(false);
      if (!this.closed) { this.emit('problem'); this.retry = setTimeout(() => this.start(), 2000); }
    });
  }
  async probe() {
    if (this.probing || this.closed) return;
    this.probing = true;
    // Debug stdout is buffered on Windows until a message arrives. -E exits on
    // SUBACK and flushes it, so broker status also works when the car is offline.
    const subscriptions = ['telemetry', 'status', 'config/state', 'command/state'].flatMap(t => ['-t', `${TOPIC}/${t}`]);
    const child = this.child('mosquitto_sub', [...subscriptions, '-q', '1', '-E', '-d']);
    let output = '';
    child.stdout.on('data', data => { if (output.length < 32768) output += data; });
    child.stderr.resume();
    const timer = setTimeout(() => child.kill(), 1800);
    child.once('error', () => this.emit('problem'));
    child.once('close', code => {
      clearTimeout(timer); this.probing = false;
      if (this.closed) return;
      const granted = output.match(/Subscribed \(mid: \d+\): ([\d, ]+)/);
      const ready = code === 0 && !!granted && granted[1].split(',').every(n => Number(n.trim()) < 128);
      this.setConnected(ready);
      if (!ready) this.emit('problem');
    });
  }
  publish(topic, payload, qos, retain) {
    if (this.closed) return Promise.reject(Error('MQTT bridge is closed.'));
    return new Promise((resolve, reject) => {
      const child = this.child('mosquitto_pub', ['-t', topic, '-q', String(qos), '-s', ...(retain ? ['-r'] : [])]);
      const timer = setTimeout(() => { child.kill(); reject(Error('MQTT publish timed out.')); }, 1200);
      child.stdout.resume(); child.stderr.resume();
      child.stdin.on('error', () => {});
      child.once('error', () => { clearTimeout(timer); reject(Error('Mosquitto client unavailable.')); });
      child.once('close', code => { clearTimeout(timer); code === 0 ? resolve() : reject(Error('MQTT publish failed. Check broker and credentials.')); });
      child.stdin.end(JSON.stringify(payload));
    });
  }
  close() { this.closed = true; clearTimeout(this.retry); clearInterval(this.probeTimer); for (const child of this.children) child.kill(); this.setConnected(false); }
}

// Fully isolated simulation: this transport has no network or process access.
export class DemoTransport extends EventEmitter {
  constructor() {
    super(); this.demo = true; this.label = 'SIMULATED BROKER'; this.sequence = 0; this.born = clock();
    this.config = { schema_version: 1, revision: 0, result: 'defaults', stop_distance_cm: 30, drive_duty: 0.35, telemetry_interval_ms: 200, driver_style: 'decide_action' };
    this.state = { schema_version: 1, last_request_id: 0, session_id: '', result: 'state', control_state: 'disarmed', motion_state: 'stopped', reason: 'boot', driver_style: 'decide_action' };
  }
  message(suffix, data, retained = false) { this.emit('message', TOPIC + '/' + suffix, structuredClone(data), retained); }
  start() {
    this.emit('connection', true); this.message('status', { schema_version: 1, online: true }, true);
    this.message('config/state', this.config, true); this.message('command/state', this.state, true);
    this.sample(); this.timer = setInterval(() => this.sample(), 50);
  }
  sample() {
    const now = clock();
    if (this.state.control_state === 'armed' && now - this.heartbeatAt >= 3000) {
      Object.assign(this.state, { control_state: 'disarmed', motion_state: 'stopped', session_id: '', reason: 'heartbeat_timeout' });
      this.message('command/state', this.state);
    }
    if (this.sampleAt && now - this.sampleAt < this.config.telemetry_interval_ms) return;
    this.sampleAt = now;
    const t = (now - this.born) / 1000;
    const distances = { left: 44 + 19 * Math.sin(t * 0.63), center: 53 + 19 * Math.sin(t * 0.43 + 1), right: 39 + 18 * Math.sin(t * 0.7 + 2) };
    let steering = 0, selected = distances.center;
    const style = this.config.driver_style;
    if (style === 'decide_action') {
      if (!(distances.center > Math.max(distances.left, distances.right))) {
        steering = distances.left > distances.right ? -90 : 90; selected = steering < 0 ? distances.left : distances.right;
      }
    } else { steering = style === 'slow_left' ? -90 : style === 'slow_right' ? 90 : -90 + (Math.floor(t * 20) % 72 <= 36 ? Math.floor(t * 20) % 72 : 72 - Math.floor(t * 20) % 72) * 5; selected = Math.min(...Object.values(distances)); }
    const armed = this.state.control_state === 'armed', blocked = selected < this.config.stop_distance_cm;
    const speed = armed && !blocked ? this.config.drive_duty : 0;
    Object.assign(this.state, { motion_state: !armed ? 'stopped' : blocked ? 'inhibited' : speed > 0 ? 'moving' : 'stopped', reason: !armed ? this.state.reason : blocked ? 'obstacle' : 'none' });
    this.message('telemetry', {
      ...this.state, sequence: ++this.sequence, uptime_ms: Math.floor(now - this.born), distance_cm: distances,
      adc_raw: Object.fromEntries(Object.entries(distances).map(([k, v]) => [k, Math.round(29000 / v)])),
      steering_deg: armed ? steering : 0,
      motor: { speed_command: speed, forward_duty: armed && blocked ? 1 : speed, backward_duty: armed && blocked ? 1 : 0 },
      closest: { sensor: Object.keys(distances).reduce((a, b) => distances[a] < distances[b] ? a : b), distance_cm: Math.min(...Object.values(distances)) },
    });
  }
  async publish(topic, data) {
    if (topic.endsWith('/config/set')) {
      const error = data.revision <= this.config.revision ? 'stale_revision' : this.state.control_state === 'armed' && data.driver_style !== this.config.driver_style ? 'driver_style_requires_disarmed' : null;
      if (!error) { this.config = { ...data, result: 'applied' }; this.state.driver_style = data.driver_style; }
      this.message('config/state', { ...this.config, revision: data.revision, result: error ? 'rejected' : 'applied', ...(error ? { error } : {}) });
    } else if (data.command === 'heartbeat') {
      if (data.session_id === this.state.session_id) this.heartbeatAt = clock();
    } else {
      const start = data.command === 'start';
      Object.assign(this.state, { last_request_id: data.request_id, session_id: start ? data.session_id : '', result: 'accepted', control_state: start ? 'armed' : 'disarmed', motion_state: 'stopped', reason: start ? 'none' : 'operator_stop' });
      this.heartbeatAt = clock(); this.message('command/state', this.state);
    }
  }
  close() { clearInterval(this.timer); this.emit('connection', false); }
}
