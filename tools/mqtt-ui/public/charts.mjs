export const colors = { left: '#48d9e8', center: '#ffad62', right: '#b4e36c', steering: '#7aaeff', speed: '#c1a0ff' };
export function valueOf(data, key) {
  const value = key === 'steering' ? data.steering_deg : key === 'speed' ? data.motor?.speed_command : data.distance_cm?.[key];
  return typeof value === 'number' && Number.isFinite(value) && (key === 'steering' || value >= 0) ? value : null;
}
export function segments(history, key, now, interval) {
  const result = []; let current = [];
  for (const sample of history) {
    if (sample.at < now - 10000 || sample.at > now) continue;
    const value = valueOf(sample.data, key);
    if (value === null || (current.length && sample.at - current.at(-1).at > Math.max(600, interval * 1.8))) {
      if (current.length) result.push(current); current = [];
    }
    if (value !== null) current.push({ at: sample.at, value });
  }
  if (current.length) result.push(current);
  return result;
}
export class Chart {
  constructor(key) {
    this.key = key; this.canvas = document.getElementById(key + '-chart'); this.hover = null;
    this.empty = this.canvas.nextElementSibling;
    this.canvas.addEventListener('pointermove', e => { this.hover = e.clientX - this.canvas.getBoundingClientRect().left; });
    this.canvas.addEventListener('pointerleave', () => { this.hover = null; });
  }
  draw(history, now, interval, stopDistance, stale) {
    const canvas = this.canvas, bounds = canvas.getBoundingClientRect(), w = bounds.width, h = bounds.height;
    if (!w || !h) return;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) { canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr); }
    const c = canvas.getContext('2d'); c.setTransform(dpr, 0, 0, dpr, 0, 0); c.clearRect(0, 0, w, h);
    const groups = segments(history, this.key, now, interval), points = groups.flat();
    this.empty.hidden = points.length > 0;
    const steering = this.key === 'steering', speed = this.key === 'speed', discrete = steering || speed;
    const low = steering ? -90 : 0;
    const peak = Math.max(80, ...points.map(p => p.value), stopDistance || 30);
    const unit = 10 ** Math.floor(Math.log10(peak));
    const high = steering ? 90 : speed ? Math.max(1, ...points.map(p => p.value)) : Math.ceil(peak / (unit / 2)) * (unit / 2);
    const left = high >= 1000 ? 42 : 34, right = w - 10, top = 15, bottom = h - 23;
    const x = at => left + ((at - now + 10000) / 10000) * (right - left);
    const y = value => bottom - (value - low) / (high - low) * (bottom - top);
    c.font = '9px Consolas, monospace'; c.lineWidth = 1;
    for (let i = 0; i <= 2; i++) {
      const value = low + (high - low) * i / 2, yy = y(value);
      c.strokeStyle = '#2a405140'; c.beginPath(); c.moveTo(left, yy); c.lineTo(right, yy); c.stroke();
      c.fillStyle = '#688298'; c.textAlign = 'right';
      c.fillText(speed ? value.toFixed(1) : String(Math.round(value)), left - 8, yy + 3);
    }
    for (let i = 0; i <= 2; i++) {
      const xx = left + (right - left) * i / 2;
      c.strokeStyle = '#2a405125'; c.beginPath(); c.moveTo(xx, top); c.lineTo(xx, bottom); c.stroke();
      c.fillStyle = '#688298'; c.textAlign = i === 0 ? 'left' : i === 2 ? 'right' : 'center';
      c.fillText(i === 0 ? '−10s' : i === 1 ? '−5s' : 'now', xx, h - 6);
    }
    if (!discrete && Number.isFinite(stopDistance)) {
      c.setLineDash([3, 5]); c.strokeStyle = '#ffad6250'; c.beginPath(); c.moveTo(left, y(stopDistance)); c.lineTo(right, y(stopDistance)); c.stroke(); c.setLineDash([]);
    }
    c.save(); c.beginPath(); c.rect(left, top - 2, right - left + 2, bottom - top + 4); c.clip();
    c.globalAlpha = stale ? .45 : 1;
    const strokePath = group => {
      c.beginPath();
      group.forEach((p, i) => { if (!i) c.moveTo(x(p.at), y(p.value)); else { if (discrete) c.lineTo(x(p.at), y(group[i - 1].value)); c.lineTo(x(p.at), y(p.value)); } });
    };
    for (const group of groups) {
      strokePath(group);
      c.lineTo(x(group.at(-1).at), bottom); c.lineTo(x(group[0].at), bottom); c.closePath();
      const gradient = c.createLinearGradient(0, top, 0, bottom); gradient.addColorStop(0, colors[this.key] + '24'); gradient.addColorStop(1, colors[this.key] + '02'); c.fillStyle = gradient; c.fill();
      strokePath(group); c.strokeStyle = colors[this.key]; c.lineWidth = 1.8; c.lineJoin = 'round'; c.stroke();
      for (const p of group) { c.beginPath(); c.arc(x(p.at), y(p.value), group.length === 1 ? 2.8 : 1.6, 0, Math.PI * 2); c.fillStyle = colors[this.key]; c.fill(); }
    }
    c.restore();
    if (this.hover !== null && this.hover >= left && this.hover <= right && points.length) {
      const nearest = points.reduce((a, b) => Math.abs(x(a.at) - this.hover) < Math.abs(x(b.at) - this.hover) ? a : b);
      const xx = x(nearest.at); c.strokeStyle = '#829eac80'; c.setLineDash([2, 3]); c.beginPath(); c.moveTo(xx, top); c.lineTo(xx, bottom); c.stroke(); c.setLineDash([]);
      const label = `${nearest.value.toFixed(speed ? 2 : 1)} ${speed ? 'duty' : steering ? 'deg' : 'cm'} · ${((nearest.at - now) / 1000).toFixed(1)}s`;
      c.font = '10px Consolas, monospace'; const width = c.measureText(label).width + 14, bx = Math.max(left, Math.min(right - width, xx - width / 2));
      c.fillStyle = '#0b1523'; c.fillRect(bx, top - 10, width, 19); c.fillStyle = colors[this.key]; c.textAlign = 'left'; c.fillText(label, bx + 7, top + 3);
    }
    const last = points.at(-1);
    canvas.setAttribute('aria-label', `${this.key}: ${last ? last.value.toFixed(2) : 'no data'}, ${points.length} received samples in the last 10 seconds${stale ? ', stale' : ''}`);
  }
}
