// LAN access chip shared by both pages. Only a browser on this computer (127.0.0.1)
// sees the toggle; other devices get the page without it.
// crypto.randomUUID() only exists on https and localhost; getRandomValues also works over plain http on the LAN.
const client = Array.from(crypto.getRandomValues(new Uint8Array(16)), b => b.toString(16).padStart(2, '0')).join('');
const chip = document.createElement('button');
chip.type = 'button'; chip.className = 'chip neutral lan-chip'; chip.hidden = true;
document.querySelector('.connection')?.prepend(chip);
let lan = null, busy = false;
function render() {
  chip.hidden = !lan?.local;
  if (!lan) return;
  chip.className = 'chip lan-chip ' + (lan.enabled ? 'warn' : 'neutral');
  chip.textContent = lan.enabled ? `LAN ON · ${lan.urls.map(url => url.slice(7)).join(' · ')}` : 'LAN OFF';
  chip.title = lan.enabled ? 'Other devices on the network can open and drive from: ' + lan.urls.join(', ') + '. Click to turn off.'
    : 'Only this computer can open the console. Click to let devices on cnb-net open it.';
  chip.disabled = busy;
}
async function refresh() {
  try { const response = await fetch('/api/lan'); lan = response.ok ? await response.json() : null; } catch { lan = null; }
  render();
}
chip.addEventListener('click', async () => {
  const enabled = !lan.enabled;
  if (enabled && !confirm('Anyone on the same network can then open this page and drive the car. Turn on LAN access?')) return;
  busy = true; render();
  try {
    const { csrf } = await (await fetch('/api/bootstrap')).json();
    const response = await fetch('/api/lan', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Cnb-Token': csrf }, body: JSON.stringify({ client, enabled }) });
    const result = await response.json();
    if (!response.ok) throw Error(result.error || 'Request failed.');
    lan = result;
  } catch (error) { alert(error.message); }
  busy = false; render();
});
setInterval(refresh, 3000);
void refresh();
