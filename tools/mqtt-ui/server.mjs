import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { randomBytes } from 'node:crypto';
import { ConsoleControl } from './core.mjs';
import { MosquittoTransport, DemoTransport, readSettings } from './transport.mjs';

const directory = path.dirname(fileURLToPath(import.meta.url));
const assets = new Map([
  ['/', ['index.html', 'text/html; charset=utf-8']],
  ['/app.mjs', ['app.mjs', 'text/javascript; charset=utf-8']],
  ['/charts.mjs', ['charts.mjs', 'text/javascript; charset=utf-8']],
  ['/style.css', ['style.css', 'text/css; charset=utf-8']],
]);

export function createConsole(transport, port = 8765) {
  const control = new ConsoleControl(transport);
  const csrf = randomBytes(24).toString('hex');
  const streams = new Set();
  let origin;
  const json = (res, status, data) => { res.writeHead(status, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(data)); };
  const server = http.createServer(async (req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('X-Content-Type-Options', 'nosniff');
    res.setHeader('Referrer-Policy', 'no-referrer');
    res.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    // Local device control: reject cross-origin requests and DNS rebinding.
    if (req.headers.host !== new URL(origin).host || (req.headers.origin && req.headers.origin !== origin) || req.headers['sec-fetch-site'] === 'cross-site') {
      json(res, 403, { error: 'Open the console at ' + origin }); return;
    }
    try {
      const route = new URL(req.url, origin).pathname;
      if (req.method === 'GET' && assets.has(route)) {
        const [file, type] = assets.get(route);
        const content = await readFile(path.join(directory, 'public', file));
        res.writeHead(200, { 'Content-Type': type }); res.end(content); return;
      }
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
      switch (route) {
        case '/api/pulse': control.pulse(data.client); break;
        case '/api/start': await control.start(data.client); break;
        case '/api/stop': await control.stop(); break;
        case '/api/config': await control.configure(data.client, data.config); break;
        case '/api/release': await control.release(data.client); break;
        default: json(res, 404, { error: 'Not found.' }); return;
      }
      json(res, 200, { ok: true, state: control.snapshot() });
    } catch (error) { json(res, 400, { error: error instanceof SyntaxError ? 'Invalid JSON request.' : error.message }); }
  });
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
    control, server,
    async listen() {
      await new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, '127.0.0.1', resolve); });
      origin = `http://127.0.0.1:${server.address().port}`;
      transport.start(); return origin;
    },
    async close() {
      clearInterval(timer);
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
  if (!Number.isInteger(port) || port < 1024 || port > 65535 || args.some((a, i) => a !== '--demo' && a !== '--port' && !(portIndex >= 0 && i === portIndex + 1))) {
    console.error('Usage: node server.mjs [--demo] [--port 8765]'); process.exit(1);
  }
  let app;
  try {
    const transport = demo ? new DemoTransport() : new MosquittoTransport(readSettings(path.join(directory, '../mqtt/.env')));
    app = createConsole(transport, port);
    const url = await app.listen();
    console.log(`CnB RC Control: ${url}\n${demo ? 'DEMO: simulated data, no MQTT connection.' : 'LIVE: waiting for car telemetry. Opening the page does not start the car.'}\nKeep this terminal open. Press Ctrl+C to close.`);
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
