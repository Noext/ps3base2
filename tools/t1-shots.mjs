#!/usr/bin/env node
/* t1-shots.mjs -- capture the game's canvas from inside the page's own rAF loop.
 *
 * The previous approach copied the canvas into a persistent WebGPU texture inside
 * the backend, kept a g_capture_want flag, and had the page poll for 30 animation
 * frames. It never met: the backend logged "capture copied to persistent texture
 * 1280x720" while the page logged "capture_now: ready 0 tex 0" on every attempt.
 *
 * This script does the simplest thing that can work. The page calls
 * canvas.toBlob() inside the SAME requestAnimationFrame callback that runs
 * replay_frame(), immediately after it and before any new getCurrentTexture() can
 * invalidate the frame. The PNG is handed here through window.__t1Shot, and this
 * script writes it with the binary fingerprint and the timestamp.
 */
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync, existsSync, appendFileSync } from 'node:fs';
import { join } from 'node:path';

const A = { seconds: 70, out: '/tmp/t1-shots', size: '1280x720', wasm: 'unknown', display: null };
const argv = process.argv.slice(2);
for (let i = 0; i < argv.length; i++) {
  const a = argv[i], next = () => argv[++i];
  switch (a) {
    case '--url': A.url = next(); break;
    case '--out': A.out = next(); break;
    case '--seconds': A.seconds = +next(); break;
    case '--size': A.size = next(); break;
    case '--wasm-md5': A.wasm = next(); break;
    case '--display': A.display = next(); break;
    default: console.error('unknown arg ' + a); process.exit(2);
  }
}
if (!A.url) { console.error('--url required'); process.exit(2); }
mkdirSync(A.out, { recursive: true });
const LOG = join(A.out, 'shots.log');
const log = (m) => { const line = '[' + new Date().toISOString() + '] ' + m; console.log(line); appendFileSync(LOG, line + '\n'); };
const parts = A.size.split('x');
const VW = Number(parts[0]), VH = Number(parts[1]);

const CHROME = (() => {
  const cands = [
    '/root/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome',
    '/root/.cache/ms-playwright/chromium-1217/chrome-linux64/chrome',
    '/root/.cache/ms-playwright/chromium-1208/chrome-linux64/chrome',
  ];
  for (const p of cands) if (existsSync(p)) return p;
  return 'chromium';
})();

let xvfb = null;
if (!A.display) {
  A.display = ':' + (60 + Math.floor(Math.random() * 20));
  xvfb = spawn('Xvfb', [A.display, '-screen', '0', VW + 'x' + VH + 'x24', '-ac', '+extension', 'GLX', '+render', '-noreset'],
               { stdio: ['ignore', 'ignore', 'ignore'] });
  await new Promise((r) => setTimeout(r, 1500));
}

const userDir = '/tmp/t1-shots-profile-' + process.pid;
const args = [
  '--remote-debugging-port=0',
  '--user-data-dir=' + userDir,
  '--no-sandbox', '--no-first-run', '--disable-dev-shm-usage',
  '--enable-unsafe-webgpu', '--enable-features=Vulkan',
  '--use-angle=vulkan', '--enable-gpu',
  '--window-size=' + VW + ',' + VH,
  'about:blank',
];
const chrome = spawn(CHROME, args, { stdio: ['ignore', 'ignore', 'pipe'], env: { ...process.env, DISPLAY: A.display } });
let wsUrl = null;
await new Promise((res, rej) => {
  let buf = '';
  chrome.stderr.on('data', (d) => {
    buf += d.toString();
    const m = /DevTools listening on (ws:\/\/\S+)/.exec(buf);
    if (m && !wsUrl) { wsUrl = m[1]; res(); }
  });
  setTimeout(() => { if (!wsUrl) rej(new Error('no DevTools port')); }, 20000);
});
log('browser up on ' + A.display);
const ws = new WebSocket(wsUrl);
await new Promise((r) => ws.addEventListener('open', r, { once: true }));
let msgId = 0;
const pending = new Map();
ws.addEventListener('message', (ev) => {
  const m = JSON.parse(ev.data);
  if (m.id && pending.has(m.id)) { const p = pending.get(m.id); pending.delete(m.id); p(m); }
});
function send(method, params, sessionId) {
  const id = ++msgId;
  return new Promise((res) => {
    pending.set(id, res);
    const msg = { id: id, method: method, params: params || {} };
    if (sessionId) msg.sessionId = sessionId;
    ws.send(JSON.stringify(msg));
  });
}

const created = await send('Target.createTarget', { url: 'about:blank' });
const att = await send('Target.attachToTarget', { targetId: created.result.targetId, flatten: true });
const sessionId = att.result.sessionId;
await send('Page.enable', {}, sessionId);
await send('Runtime.enable', {}, sessionId);
await send('Log.enable', {}, sessionId);
await send('Emulation.setDeviceMetricsOverride', { width: VW, height: VH, deviceScaleFactor: 1, mobile: false }, sessionId);

const HOOK = [
  'window.__t1Shot = function (meta, b64) {',
  '  window.__t1Shots = window.__t1Shots || [];',
  '  window.__t1Shots.push({ meta: meta, b64: b64 });',
  '  return window.__t1Shots.length;',
  '};',
  'window.__t1Drain = function () { var s = window.__t1Shots || []; window.__t1Shots = []; return s; };',
].join('\n');
await send('Page.addScriptToEvaluateOnNewDocument', { source: HOOK }, sessionId);

ws.addEventListener('message', (ev) => {
  const m = JSON.parse(ev.data);
  if (m.method === 'Runtime.consoleAPICalled') {
    const txt = (m.params.args || []).map((a) => a.value !== undefined ? a.value : (a.description || '')).join(' ');
    if (txt) log('page: ' + txt.slice(0, 400));
  }
});

await send('Page.navigate', { url: A.url }, sessionId);
log('navigated to ' + A.url + ', recording ' + A.seconds + ' s');

const evalIn = async (expr, awaitPromise) => {
  const r = await send('Runtime.evaluate', { expression: expr, awaitPromise: !!awaitPromise, returnByValue: true }, sessionId);
  if (r.result && r.result.exceptionDetails) throw new Error(r.result.exceptionDetails.text || 'eval failed');
  return r.result && r.result.result ? r.result.result.value : undefined;
};

try { await evalIn('window.__T1_WASM_MD5 = ' + JSON.stringify(A.wasm) + '; true'); }
catch (e) { log('fingerprint stamp failed: ' + e.message); }

const deadline = Date.now() + A.seconds * 1000;
let saved = 0;
const index = [];
while (Date.now() < deadline) {
  await new Promise((r) => setTimeout(r, 1000));
  let batch = null;
  try { batch = await evalIn('JSON.stringify(window.__t1Drain ? window.__t1Drain() : [])', false); }
  catch (e) { log('drain failed (page busy?): ' + e.message); continue; }
  if (!batch) continue;
  let arr = [];
  try { arr = JSON.parse(batch); } catch (e) { continue; }
  for (const item of arr) {
    if (!item || !item.b64) continue;
    const m = item.meta || {};
    const name = 'shot-' + String(saved).padStart(3, '0') + '-' + A.wasm + '.png';
    const file = join(A.out, name);
    writeFileSync(file, Buffer.from(item.b64, 'base64'));
    index.push({ file: name, idx: m.idx, at: m.at, wasm: m.wasm || A.wasm, bytes: m.bytes, w: m.w, h: m.h });
    log('SHOT ' + name + ' at=' + m.at + ' wasm=' + (m.wasm || A.wasm) + ' bytes=' + m.bytes + ' ' + m.w + 'x' + m.h);
    saved++;
  }
}
writeFileSync(join(A.out, 'shots.json'), JSON.stringify({ url: A.url, wasm: A.wasm, seconds: A.seconds, shots: index }, null, 1));
log('saved ' + saved + ' shots to ' + A.out);
try { ws.close(); } catch (e) {}
chrome.kill('SIGKILL');
if (xvfb) xvfb.kill('SIGKILL');
process.exit(saved > 0 ? 0 : 4);