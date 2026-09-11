import { EventEmitter } from 'node:events';
import { randomBytes } from 'node:crypto';

export const TOPIC = 'cnb/vagrant';
export const STYLES = ['decide_action', 'slow_left', 'slow_right', 'gradual_sweep'];
export const clock = () => performance.timeOrigin + performance.now();
const uint = n => Number.isInteger(n) && n >= 0 && n <= 0xffffffff;

export function validateConfig(value) {
  const keys = ['stop_distance_cm', 'drive_duty', 'telemetry_interval_ms', 'driver_style'];
  if (!value || Object.keys(value).some(k => !keys.includes(k))) throw Error('Unknown configuration field.');
  for (const [key, min, max] of [['stop_distance_cm', 30, 70], ['drive_duty', 0, 1], ['telemetry_interval_ms', 200, 5000]]) {
    if (!Number.isFinite(value[key]) || value[key] < min || value[key] > max) throw Error(`${key}: allowed range ${min}–${max}.`);
  }
  if (!Number.isInteger(value.telemetry_interval_ms) || !STYLES.includes(value.driver_style)) throw Error('Invalid interval or driver style.');
  return Object.fromEntries(keys.map(k => [k, value[k]]));
}

// Ten seconds of RECEIVED samples. Nulls and missing packets remain gaps.
export class History {
  samples = [];
  last = null;
  ingest(data, now) {
    if (!uint(data.sequence) || !Number.isFinite(data.uptime_ms) || data.uptime_ms < 0) return false;
    if (this.last && data.sequence === this.last.sequence && data.uptime_ms === this.last.uptime_ms) return false;
    if (this.last && data.uptime_ms < this.last.uptime_ms) {
      // A lower uptime AND a lower sequence is a reboot; old packets otherwise drop.
      if (data.sequence >= this.last.sequence) return false;
      this.samples = [];
    } else if (this.last && data.sequence < this.last.sequence && this.last.sequence < 0xfffffff0) return false;
    this.last = data;
    this.samples.push({ at: now, data });
    this.prune(now);
    return true;
  }
  prune(now) { this.samples = this.samples.filter(p => p.at >= now - 10000).slice(-200); }
  clear() { this.samples = []; this.last = null; }
}

export class ConsoleControl extends EventEmitter {
  constructor(transport, { now = clock, ackMs = 1800 } = {}) {
    super();
    this.transport = transport; this.now = now; this.ackMs = ackMs;
    this.history = new History(); this.pending = new Map(); this.highWater = 0;
    this.connected = false; this.online = false; this.receivedAt = 0;
    this.config = null; this.command = null; this.commandAt = 0; this.telemetry = null;
    this.owner = null; this.lastHeartbeatAt = 0; this.heartbeatBusy = false;
    this.notice = 'Waiting for MQTT connection.';
    transport.on('connection', connected => {
      this.connected = connected;
      this.online = false; this.receivedAt = 0; this.telemetry = null;
      this.history.clear(); this.command = null; this.config = null;
      this.cancel('Broker connection changed. Start again after reconnection.');
      this.notice = connected ? 'Connected. Waiting for fresh car telemetry.' : 'Broker disconnected. Check Mosquitto and tools/mqtt/.env.';
    });
    transport.on('message', (topic, data, retained = false) => this.receive(topic, data, retained));
    transport.on('problem', () => { this.notice = 'MQTT connection failed. Check broker, address and credentials in tools/mqtt/.env.'; });
  }
  id() {
    this.highWater = Math.max(Math.floor(this.now() / 1000), this.highWater + 1);
    if (this.highWater > 0xffffffff) throw Error('MQTT request counter exhausted.');
    return this.highWater;
  }
  receive(topic, data, retained) {
    if (!data || typeof data !== 'object' || data.schema_version !== 1) return;
    const suffix = topic.slice(TOPIC.length + 1);
    if (!topic.startsWith(TOPIC + '/')) return;
    if (suffix === 'status' && typeof data.online === 'boolean') {
      this.online = data.online;
      if (!data.online) { this.receivedAt = 0; this.cancel('Car went offline.'); }
    }
    if (suffix === 'telemetry' && !retained && this.history.ingest(data, this.now())) {
      this.telemetry = data; this.receivedAt = this.now();
      if (this.notice === 'Connected. Waiting for fresh car telemetry.') this.notice = 'Receiving telemetry. Choose settings, then start when ready.';
      if (this.owner?.armed && data.control_state !== 'armed') this.cancel('Car disarmed: ' + (data.reason || 'unknown'));
    }
    if (suffix === 'config/state' && uint(data.revision)) {
      try { validateConfig(Object.fromEntries(['stop_distance_cm', 'drive_duty', 'telemetry_interval_ms', 'driver_style'].map(k => [k, data[k]]))); }
      catch { return; }
      this.config = data; this.highWater = Math.max(this.highWater, data.revision);
      if (!retained) this.pending.get('config:' + data.revision)?.resolve(data);
    }
    if (suffix === 'command/state' && uint(data.last_request_id)) {
      this.command = data; this.commandAt = this.now(); this.highWater = Math.max(this.highWater, data.last_request_id);
      if (!retained) {
        this.pending.get('command:' + data.last_request_id)?.resolve(data);
        if (this.owner?.armed && (data.control_state !== 'armed' || data.session_id !== this.owner.session)) {
          this.cancel('Control session ended: ' + (data.reason || 'another controller'));
        }
      }
    }
  }
  fresh() {
    return this.connected && this.online && this.receivedAt > 0 && this.now() - this.receivedAt <= Math.max(2500, (this.config?.telemetry_interval_ms || 1000) * 2.5);
  }
  snapshot() {
    this.history.prune(this.now());
    return {
      now: this.now(), demo: this.transport.demo === true,
      broker: { connected: this.connected, label: this.transport.label },
      car: { online: this.online, fresh: this.fresh(), receivedAt: this.receivedAt },
      config: this.config, command: this.command, commandAt: this.commandAt, telemetry: this.telemetry,
      owner: this.owner ? { client: this.owner.client, session: this.owner.session, armed: this.owner.armed } : null,
      lastHeartbeatAt: this.lastHeartbeatAt, notice: this.notice, history: this.history.samples,
    };
  }
  cancel(message) {
    this.owner = null; this.lastHeartbeatAt = 0;
    for (const pending of this.pending.values()) pending.reject(Error(message));
    this.pending.clear(); this.notice = message;
  }
  async acknowledged(kind, id, topic, payload, retain = false) {
    const key = kind + ':' + id;
    let timer;
    const reply = new Promise((resolve, reject) => {
      timer = setTimeout(() => reject(Error('No matching acknowledgement from the car.')), this.ackMs);
      this.pending.set(key, { resolve, reject });
    });
    // Register before publishing, including synchronous test transports.
    try {
      const [state] = await Promise.all([reply, this.transport.publish(topic, payload, 1, retain)]);
      return state;
    } finally { clearTimeout(timer); this.pending.delete(key); }
  }
  pulse(client) { if (this.owner?.client === client) this.owner.lastSeen = this.now(); }
  async start(client) {
    if (!this.fresh() || !this.config || !this.command) throw Error('Wait for fresh telemetry and configuration from the car.');
    if (this.owner || this.telemetry.control_state === 'armed' || this.command.control_state === 'armed') throw Error('Already armed or controlled elsewhere. Stop before starting a new session.');
    const owner = { client, session: randomBytes(8).toString('hex'), armed: false, lastSeen: this.now() };
    this.owner = owner;
    const request_id = this.id();
    try {
      const state = await this.acknowledged('command', request_id, TOPIC + '/command', {
        schema_version: 1, request_id, session_id: owner.session, command: 'start',
      });
      if (this.owner !== owner) throw Error('Start cancelled.');
      if (state.result !== 'accepted' || state.session_id !== owner.session || state.control_state !== 'armed') throw Error('Start rejected: ' + (state.error || state.reason));
      owner.armed = true; this.notice = 'Start acknowledged by car. Heartbeat active.';
    } catch (error) {
      if (this.owner === owner) {
        this.owner = null;
        await this.sendStop(owner.session).catch(() => {});
      }
      throw error;
    }
  }
  sendStop(session = randomBytes(8).toString('hex')) {
    return this.transport.publish(TOPIC + '/command', {
      schema_version: 1, request_id: this.id(), session_id: session, command: 'stop',
    }, 1, false);
  }
  async stop() {
    const session = this.owner?.session || randomBytes(8).toString('hex');
    this.cancel('Stop requested; waiting for the car.');
    const request_id = this.id();
    const state = await this.acknowledged('command', request_id, TOPIC + '/command', {
      schema_version: 1, request_id, session_id: session, command: 'stop',
    });
    if (state.result !== 'accepted' || state.control_state !== 'disarmed') throw Error('Stop was not confirmed by the car.');
    this.notice = 'Stop acknowledged by car.';
  }
  async configure(client, input) {
    const config = validateConfig(input);
    if (!this.fresh() || !this.config) throw Error('Wait for fresh telemetry and configuration.');
    if (this.owner && this.owner.client !== client) throw Error('Another browser tab controls the car.');
    if ((this.telemetry.control_state === 'armed' || this.command?.control_state === 'armed') && config.driver_style !== this.config.driver_style) throw Error('Stop the car before changing driver style.');
    if ([...this.pending.keys()].some(k => k.startsWith('config:'))) throw Error('Configuration acknowledgement still pending.');
    const revision = this.id();
    const state = await this.acknowledged('config', revision, TOPIC + '/config/set', { schema_version: 1, revision, ...config }, true);
    if (state.result !== 'applied') throw Error('Configuration rejected: ' + (state.error || 'unknown'));
    this.notice = `Configuration #${revision} acknowledged by car.`;
  }
  async release(client) {
    if (this.owner?.client !== client) return;
    const session = this.owner.session;
    this.cancel('Control tab closed or left the foreground. Start again to resume.');
    await this.sendStop(session).catch(() => {});
  }
  async tick() {
    const owner = this.owner;
    if (!owner) return;
    if (this.now() - owner.lastSeen > 1600 || !this.fresh()) { await this.release(owner.client); return; }
    if (owner.armed && !this.heartbeatBusy && this.now() - this.lastHeartbeatAt >= 900) {
      this.heartbeatBusy = true;
      try {
        await this.transport.publish(TOPIC + '/command', { schema_version: 1, session_id: owner.session, command: 'heartbeat' }, 0, false);
        if (this.owner === owner) this.lastHeartbeatAt = this.now();
      } catch { await this.release(owner.client); }
      finally { this.heartbeatBusy = false; }
    }
  }
}
