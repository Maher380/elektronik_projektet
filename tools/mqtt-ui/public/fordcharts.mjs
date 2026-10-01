// Ford page charts: the last 30 seconds of what the car was asked to do beside what the
// Pi measured. Each series has its own axis range; nulls and missing samples are gaps.
export const WINDOW_MS = 30000;
const GAP_MS = 600;

export function segments(points, now) {
  const result = []; let current = [];
  for (const p of points) {
    if (p.at < now - WINDOW_MS || p.at > now) continue;
    if (p.value === null || (current.length && p.at - current.at(-1).at > GAP_MS)) {
      if (current.length) result.push(current); current = [];
    }
    if (p.value !== null) current.push(p);
  }
  if (current.length) result.push(current);
  return result;
}

export class FordChart {
  // series: [{ color, range: values => [low, high], format: value => string, side: 'left' | 'right' | null }]
  constructor(id, series) {
    this.canvas = document.getElementById(id); this.empty = this.canvas.nextElementSibling; this.series = series;
  }
  draw(lines, now, label) {
    const canvas = this.canvas, bounds = canvas.getBoundingClientRect(), w = bounds.width, h = bounds.height;
    if (!w || !h) return;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) { canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr); }
    const c = canvas.getContext('2d'); c.setTransform(dpr, 0, 0, dpr, 0, 0); c.clearRect(0, 0, w, h);
    const groups = lines.map(points => segments(points, now));
    this.empty.hidden = groups.some(g => g.length);
    const left = 44, right = w - 44, top = 12, bottom = h - 23;
    const x = at => left + ((at - now + WINDOW_MS) / WINDOW_MS) * (right - left);
    c.font = '9px Consolas, monospace'; c.lineWidth = 1;
    for (let i = 0; i <= 3; i++) {
      const xx = left + (right - left) * i / 3;
      c.strokeStyle = '#2a405125'; c.beginPath(); c.moveTo(xx, top); c.lineTo(xx, bottom); c.stroke();
      c.fillStyle = '#688298'; c.textAlign = i === 0 ? 'left' : i === 3 ? 'right' : 'center';
      c.fillText(i === 3 ? 'now' : `−${(3 - i) * 10}s`, xx, h - 6);
    }
    this.series.forEach((s, index) => {
      const [low, high] = s.range(groups[index].flat().map(p => p.value));
      const y = value => bottom - (value - low) / (high - low) * (bottom - top);
      for (let i = 0; i <= 2; i++) {
        const value = low + (high - low) * i / 2, yy = y(value);
        if (index === 0) { c.strokeStyle = '#2a405140'; c.beginPath(); c.moveTo(left, yy); c.lineTo(right, yy); c.stroke(); }
        if (!s.side) continue;
        c.fillStyle = s.color; c.textAlign = s.side === 'left' ? 'right' : 'left';
        c.fillText(s.format(value), s.side === 'left' ? left - 6 : right + 6, yy + 3);
      }
      c.save(); c.beginPath(); c.rect(left, top - 2, right - left + 2, bottom - top + 4); c.clip();
      for (const group of groups[index]) {
        c.beginPath();
        group.forEach((p, i) => i ? c.lineTo(x(p.at), y(p.value)) : c.moveTo(x(p.at), y(p.value)));
        c.strokeStyle = s.color; c.lineWidth = 1.8; c.lineJoin = 'round'; c.stroke();
        if (group.length === 1) { c.beginPath(); c.arc(x(group[0].at), y(group[0].value), 2.4, 0, Math.PI * 2); c.fillStyle = s.color; c.fill(); }
      }
      c.restore();
    });
    canvas.setAttribute('aria-label', label);
  }
}
