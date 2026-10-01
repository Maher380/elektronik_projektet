import http from 'node:http';
import os from 'node:os';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { randomBytes } from 'node:crypto';
import { CARS, ConsoleControl, topicFor } from './core.mjs';
import { MosquittoTransport, DemoTransport, FordDemoTransport, readSettings } from './transport.mjs';

const directory = path.dirname(fileURLToPath(import.meta.url));
const shared = [['/style.css', ['style.css', 'text/css; charset=utf-8']], ['/lan.mjs', ['lan.mjs', 'text/javascript; charset=utf-8']]];
const pages = {
  vagrant: new Map([...shared,
    ['/', ['index.html', 'text/html; charset=utf-8']],
    ['/app.mjs', ['app.mjs', 'text/javascript; charset=utf-8']],
    ['/charts.mjs', ['charts.mjs', 'text/javascript; charset=utf-8']]]),
  ford: new Map([...shared,
    ['/', ['ford.html', 'text/html; charset=utf-8']],
    ['/ford.mjs', ['ford.mjs', 'text/javascript; charset=utf-8']],
    ['/ford.css', ['ford.css', 'text/css; charset=utf-8']]]),
};

// IPv4 addresses on private networks (e.g. the cnb-net hotspot) that LAN access listens on.
const privateAddress = ip => /^(10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.)/.test(ip);
export function lanAddresses() {
  return Object.values(os.networkInterfaces()).flat()
    .filter(i => i && i.family === 'IPv4' && !i.internal && privateAddress(i.address)).map(i => i.address);
}

export function createConsole(transport, port = 8765, car = 'vagrant', { addresses = lanAddresses } = {}) {
  const control = new ConsoleControl(transport, { car });
  const assets = pages[car];
  const csrf = randomBytes(24).toString('hex');
  const streams = new Set();
  // LAN access is off at start. Extra servers on the private addresses are opened and
  // closed from the page, and only by a browser on this computer (127.0.0.1).
  const lan = new Map();
  let origin;
  const lanState = () => ({ enabled: lan.size > 0, urls: [...lan.keys()].map(host => `http://${host}`) });
  const local = req => req.socket.remoteAddress === '127.0.0.1' && req.headers.host === new URL(origin).host;
  async function setLan(enabled) {
    if (!enabled) {
      const servers = [...lan.values()]; lan.clear();
      await Promise.all(servers.map(s => { s.closeAllConnections(); return new Promise(resolve => s.close(resolve)); }));
      return;
    }
    if (lan.size) return;
    for (const address of addresses()) {
      const extra = http.createServer(handle);
      extra.requestTimeout = 5000; extra.headersTimeout = 5000;
      try {
        await new Promise((resolve, reject) => { extra.once('error', reject); extra.listen(server.address().port, address, resolve); });
        lan.set(`${address}:${extra.address().port}`, extra);
      } catch { /* Address gone or port taken on that interface; skip it. */ }
    }
    if (!lan.size) throw Error('No private network address found. Connect this computer to cnb-net.');
  }
  const json = (res, status, data) => { res.writeHead(status, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(data)); };
  async function handle(req, res) {
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('X-Content-Type-Options', 'nosniff');
    res.setHeader('Referrer-Policy', 'no-referrer');
    res.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    // Device control: reject cross-origin requests and DNS rebinding. A LAN host is only
    // accepted on the server bound to that address.
    const host = req.headers.host, own = `http://${host}`;
    const known = host === new URL(origin).host ? req.socket.localAddress === '127.0.0.1' : lan.get(host)?.listening && req.socket.localAddress === host.split(':')[0];
    if (!known || (req.headers.origin && req.headers.origin !== own) || req.headers['sec-fetch-site'] === 'cross-site') {
      json(res, 403, { error: 'Open the console at ' + origin }); return;
    }
    try {
      const route = new URL(req.url, own).pathname;
      if (req.method === 'GET' && assets.has(route)) {
        const [file, type] = assets.get(route);
        const content = await readFile(path.join(directory, 'public', file));
        res.writeHead(200, { 'Content-Type': type }); res.end(content); return;
      }
      if (req.method === 'GET' && route === '/api/lan') { json(res, 200, { ...lanState(), local: local(req) }); return; }
      if (req.method === 'GET' && route === '/api/bootstrap') { json(res, 200, { csrf, state: control.snapshot() }); return; }
      if (req.method === 'GET' && route === '/api/events') {
        if (streams.size >= 8) { json(res, 429, { error: 'Too many console tabs.' }); return; }
        res.writeHead(200, { 'Content-Type': 'text/event-stream', Connection: 'keep-alive' });
        res.write(`data: ${JSON.stringify(control.snapshot())}\n\n`);
        streams.add(res); req.on('close', () => streams.delete(res)); return;
      }
      if (req.method !== 'POST' || !route.startsWith('/api/')) { json(res, 404, { error: 'Not found.' }); return; }
      if (req.headers['x-cnb-token'] !== csrf || req.headers['content-type'] !== 'application/json') {
        json(res, 403, { error: 'Refresh the console before sending commands.' }); return;
      }
      let body = '';
      for await (const chunk of req) { body += chunk; if (body.length > 4096) { json(res, 413, { error: 'Request too large.' }); return; } }
      const data = JSON.parse(body);
      if (!data || typeof data.client !== 'string' || !/^[a-f0-9-]{16,40}$/.test(data.client)) throw Error('Invalid browser session.');
      if (route === '/api/lan') {
        if (!local(req)) { json(res, 403, { error: 'LAN access can only be changed at ' + origin }); return; }
        if (typeof data.enabled !== 'boolean') throw Error('Invalid LAN request.');
        await setLan(data.enabled); json(res, 200, { ...lanState(), local: true }); return;
      }
      // Streamed every 100 ms, so it answers without the full state snapshot.
      if (route === '/api/drive') { json(res, 200, { ok: await control.drive(data.client, data.steering, data.speed) }); return; }
      switch (route) {
        case '/api/pulse': control.pulse(data.client); break;
        case '/api/start': await control.start(data.client); break;
        case '/api/stop': await control.stop(); break;
        case '/api/servo': await control.servo(data.client, data.angle); break;
        case '/api/config': await control.configure(data.client, data.config); break;
        case '/api/release': await control.release(data.client); break;
        default: json(res, 404, { error: 'Not found.' }); return;
      }
      json(res, 200, { ok: true, state: control.snapshot() });
    } catch (error) { json(res, 400, { error: error instanceof SyntaxError ? 'Invalid JSON request.' : error.message }); }
  }
  const server = http.createServer(handle);
  server.requestTimeout = 5000; server.headersTimeout = 5000;
  const timer = setInterval(() => {
    void control.tick();
    const message = `data: ${JSON.stringify(control.snapshot())}\n\n`;
    for (const res of streams) {
      if (res.writableLength > 128000 || res.destroyed) { res.destroy(); streams.delete(res); }
      else res.write(message);
    }
  }, 250);
  return {
    control, server, setLan, lanState,
    async listen() {
      await new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, '127.0.0.1', resolve); });
      origin = `http://127.0.0.1:${server.address().port}`;
      transport.start(); return origin;
    },
    async close() {
      clearInterval(timer);
      await setLan(false);
      if (control.owner) await control.release(control.owner.client);
      control.cancel('Console closed.'); transport.close();
      for (const res of streams) res.end();
      server.closeAllConnections();
      await new Promise(resolve => server.close(resolve));
    },
  };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  const demo = args.includes('--demo');
  const portIndex = args.indexOf('--port');
  const port = portIndex >= 0 ? Number(args[portIndex + 1]) : 8765;
  const carIndex = args.indexOf('--car');
  const car = carIndex >= 0 ? args[carIndex + 1] : 'vagrant';
  const valueIndex = i => (portIndex >= 0 && i === portIndex + 1) || (carIndex >= 0 && i === carIndex + 1);
  if (!Number.isInteger(port) || port < 1024 || port > 65535 || !CARS.includes(car) || args.some((a, i) => !['--demo', '--port', '--car'].includes(a) && !valueIndex(i))) {
    console.error('Usage: node server.mjs [--demo] [--port 8765] [--car vagrant|ford]'); process.exit(1);
  }
  let app;
  try {
    const transport = demo ? (car === 'ford' ? new FordDemoTransport() : new DemoTransport())
      : new MosquittoTransport(readSettings(path.join(directory, '../mqtt/.env')), topicFor(car));
    app = createConsole(transport, port, car);
    const url = await app.listen();
    console.log(`CnB RC Control (${car}): ${url}\n${demo ? 'DEMO: simulated data, no MQTT connection.' : 'LIVE: waiting for car telemetry. Opening the page does not start the car.'}\nKeep this terminal open. Press Ctrl+C to close.`);
  } catch (error) {
    console.error(error.code === 'EADDRINUSE' ? 'Port already in use. Close the other console or use --port 8766.' : 'Could not start console. Check Node.js, port and tools/mqtt/.env.');
    process.exit(1);
  }
  let closing = false;
  for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, async () => {
    if (closing) return; closing = true;
    await app.close(); process.exit(0);
  });
}
