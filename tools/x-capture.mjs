#!/usr/bin/env node
/* x-capture.mjs -- one reusable capture harness for every track.
 *
 * WHY THIS FILE EXISTS
 * A WebGPU canvas cannot be captured by the compositor when Chromium runs
 * headless: Page.captureScreenshot() returns a fully transparent image for a
 * canvas that is demonstrably drawing (measured in this track, and already
 * measured by track G). The canvas pixels therefore have to be read back from
 * the GPU itself, through a texture created with COPY_SRC:
 *
 *     the page renders twice -> once to the canvas (what the compositor shows),
 *     once to an offscreen texture (RENDER_ATTACHMENT | COPY_SRC)
 *     the harness -> copyTextureToBuffer + mapAsync -> raw RGBA -> PNG
 *
 * The page owns the WebGPU device, so the harness cannot read the texture for
 * it. Every page therefore exposes ONE function, by default
 * window.__x6Readback(), which returns { w, h, format, b64, meta } where b64 is
 * base64 RGBA8, top-left origin, rows of w*4 bytes. See
 * docs/explore/X6/capture-demo.html for a 60-line reference implementation.
 *
 * Usage:
 *   node tools/x-capture.mjs --url http://127.0.0.1:8199/x.html --out /tmp/shots
 *   node tools/x-capture.mjs --url URL --out DIR --seconds 60 --interval 5
 *   node tools/x-capture.mjs --url URL --out DIR --headless      (see caveat)
 *   node tools/x-capture.mjs --url URL --out DIR --keys Enter,ArrowDown
 *   node tools/x-capture.mjs --url URL --out DIR --auto-hook docs/explore/X6/x6-inpage-hook.js
 *
 * Outputs, in --out:
 *   readback-000.png     GPU pixels, the faithful capture
 *   compositor-000.png   Page.captureScreenshot; valid only with a display
 *   page-canvas-000-0.png canvas.toDataURL() from the page, if it returns pixels
 *   capture.log          every console line and every capture decision
 *   proof.json           per-capture measured proof (mean, centre pixel, ...)
 *
 * Requires: node >= 22 (built-in WebSocket) and ImageMagick 'convert', or pass
 * --raw to keep the .rgba files and convert them yourself.
 *
 * Exit codes: 0 ok, 2 usage, 3 no DevTools port, 4 no readback produced.
 */
import { spawn, execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, existsSync, writeFileSync, mkdirSync, appendFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

/* ------------------------------------------------------------------ args */
function usage(msg) {
  if (msg) console.error('x-capture: ' + msg);
  console.error('usage: node tools/x-capture.mjs --url URL --out DIR [--seconds N] [--interval S]');
  console.error('       [--display :N] [--headless] [--size WxH] [--keys a,b] [--keys-delay S]');
  console.error('       [--hook __x6Readback] [--no-compositor] [--no-page-canvas] [--raw]');
  process.exit(2);
}
const A = { seconds: 20, interval: 3, size: '1280x720', keys: '', keysDelay: 2, hook: '__x6Readback' };
const argv = process.argv.slice(2);
for (let i = 0; i < argv.length; i++) {
  const a = argv[i], next = () => { const v = argv[++i]; if (v === undefined) usage('missing value for ' + a); return v; };
  switch (a) {
    case '--url': A.url = next(); break;
    case '--out': A.out = next(); break;
    case '--seconds': A.seconds = +next(); break;
    case '--interval': A.interval = +next(); break;
    case '--display': A.display = next(); break;
    case '--headless': A.headless = true; break;
    case '--size': A.size = next(); break;
    case '--keys': A.keys = next(); break;
    case '--keys-delay': A.keysDelay = +next(); break;
    case '--hook': A.hook = next(); break;
    case '--auto-hook': A.autoHook = next(); break;
    case '--no-compositor': A.noCompositor = true; break;
    case '--no-page-canvas': A.noCanvas = true; break;
    case '--raw': A.raw = true; break;
    case '-h': case '--help': usage(); break;
    default: usage('unknown argument ' + a);
  }
}
if (!A.url || !A.out) usage('--url and --out are required');
const OUT = resolve(A.out);
mkdirSync(OUT, { recursive: true });
const LOG = join(OUT, 'capture.log');
writeFileSync(LOG, '');
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(2) + 's';
function log(line) { const s = '[' + stamp() + '] ' + line; appendFileSync(LOG, s + '\n'); console.log(s); }
const proof = [];

const SIZE = A.size.split('x').map(Number);
const VW = SIZE[0], VH = SIZE[1];
if (!VW || !VH) usage('bad --size');

/* ------------------------------------------------------------- display */
/* Headed Chromium needs an X display; Xvfb is enough and is what this track
 * uses. A requested or inherited display is verified with xdpyinfo before the
 * browser starts, so a typo fails here instead of as a black capture. */
let xvfb = null;
function displayWorks(d) {
  try { execFileSync('xdpyinfo', ['-display', d], { stdio: 'ignore', timeout: 5000 }); return true; }
  catch (e) { return false; }
}
function freeDisplay() {
  for (let n = 100; n < 160; n++) if (!existsSync('/tmp/.X11-unix/X' + n)) return ':' + n;
  throw new Error('no free X display number between 100 and 159');
}
const sleepMs = (ms) => new Promise((r) => setTimeout(r, ms));
if (!A.headless) {
  A.display = A.display || process.env.DISPLAY || null;
  if (!A.display || !displayWorks(A.display)) {
    const d = freeDisplay();
    A.display = d;
    xvfb = spawn('Xvfb', [d, '-screen', '0', VW + 'x' + VH + 'x24', '-ac', '+extension', 'GLX', '+render', '-noreset'],
                 { stdio: ['ignore', 'ignore', 'pipe'] });
    let xerr = ''; xvfb.stderr.on('data', (x) => { xerr += x; });
    for (let i = 0; i < 100 && !displayWorks(d); i++) execFileSync('sleep', ['0.1']);
    if (!displayWorks(d)) { log('Xvfb failed on ' + d + ': ' + String(xerr).slice(0, 400)); xvfb.kill('SIGKILL'); process.exit(3); }
    log('display: spawned Xvfb ' + d + ' ' + VW + 'x' + VH + 'x24 (pid ' + xvfb.pid + ')');
  } else {
    log('display: using existing ' + A.display);
  }
} else {
  log('display: headless (--headless). The compositor does NOT compose the canvas here; ' +
      'readback-*.png is the only faithful output, compositor-*.png is expected transparent.');
}

/* -------------------------------------------------------------- chromium */
const CHROME = process.env.CHROME || '/root/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome';
if (!existsSync(CHROME)) { log('chrome not found at ' + CHROME + ' (set $CHROME)'); process.exit(3); }
const userDir = mkdtempSync(join(tmpdir(), 'x-capture-'));
const args = [
  '--no-sandbox', '--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-angle=vulkan',
  '--remote-debugging-port=0', '--user-data-dir=' + userDir,
  '--window-size=' + VW + ',' + VH, '--window-position=0,0',
  '--no-first-run', '--no-default-browser-check', '--disable-dev-shm-usage',
  '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
  '--mute-audio', 'about:blank'
];
if (A.headless) args.unshift('--headless=new');
const env = { ...process.env };
if (!A.headless) env.DISPLAY = A.display; else delete env.DISPLAY;
const chrome = spawn(CHROME, args, { stdio: ['ignore', 'ignore', 'pipe'], env });
let chromeErr = ''; chrome.stderr.on('data', (d) => { chromeErr += d; });
function cleanup() {
  try { chrome.kill('SIGKILL'); } catch (e) {}
  if (xvfb) { try { xvfb.kill('SIGKILL'); } catch (e) {} }
  try { rmSync(userDir, { recursive: true, force: true }); } catch (e) {}
}
process.on('exit', cleanup);
process.on('SIGINT', () => { cleanup(); process.exit(130); });

const portFile = join(userDir, 'DevToolsActivePort');
let port = 0;
for (let i = 0; i < 300; i++) {
  if (existsSync(portFile)) {
    const first = readFileSync(portFile, 'utf8').split('\n')[0];
    if (first) { port = +first; break; }
  }
  execFileSync('sleep', ['0.1']);
}
if (!port) { log('no DevTools port; chrome stderr tail: ' + String(chromeErr).slice(-1200)); process.exit(3); }
log('chrome: ' + CHROME + ' (pid ' + chrome.pid + ') devtools on 127.0.0.1:' + port);

/* ------------------------------------------------------------------- CDP */
const version = await (await fetch('http://127.0.0.1:' + port + '/json/version')).json();
const ws = new WebSocket(version.webSocketDebuggerUrl);
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('devtools websocket failed')); });
let msgId = 0;
const pending = new Map();
let sessionId = null;
const pageLogs = [];
ws.onmessage = (m) => {
  const d = JSON.parse(m.data);
  if (d.id && pending.has(d.id)) {
    const p = pending.get(d.id); pending.delete(d.id);
    if (d.error) p.rej(new Error(d.error.message)); else p.res(d.result);
    return;
  }
  if (d.method === 'Runtime.consoleAPICalled') {
    const text = d.params.args.map((a) => (a.value !== undefined ? a.value : a.description || '')).join(' ');
    pageLogs.push('console.' + d.params.type + ' ' + text);
    log('page console.' + d.params.type + ' ' + String(text).slice(0, 300));
  } else if (d.method === 'Runtime.exceptionThrown') {
    const t = d.params.exceptionDetails;
    log('page exception ' + String((t.exception && t.exception.description) || t.text).slice(0, 400));
  } else if (d.method === 'Log.entryAdded') {
    log('page log ' + d.params.entry.level + ' ' + String(d.params.entry.text).slice(0, 300));
  } else if (d.method === 'Inspector.targetCrashed') {
    log('page CRASHED (Inspector.targetCrashed): the renderer or the wasm module died');
  }
};
ws.onclose = () => { log('devtools socket closed'); for (const p of pending.values()) p.rej(new Error('devtools socket closed')); pending.clear(); };
/* Every CDP call is bounded: a renderer that crashes mid-capture makes the
 * socket go quiet, and a harness that then waits forever is worse than useless. */
const CALL_TIMEOUT_MS = 30000;
const send = (method, params, sid) =>
  new Promise((res, rej) => {
    const i = ++msgId;
    const timer = setTimeout(() => {
      pending.delete(i);
      rej(new Error('CDP ' + method + ' timed out after ' + CALL_TIMEOUT_MS + ' ms'));
    }, CALL_TIMEOUT_MS);
    pending.set(i, {
      res: (v) => { clearTimeout(timer); res(v); },
      rej: (e) => { clearTimeout(timer); rej(e); },
    });
    if (ws.readyState !== 1) { pending.delete(i); clearTimeout(timer); rej(new Error('devtools socket closed')); return; }
    ws.send(JSON.stringify({ id: i, method, params: params || {}, ...(sid ? { sessionId: sid } : {}) }));
  });

try {
  const created = await send('Target.createTarget', { url: 'about:blank' }, null);
  const att = await send('Target.attachToTarget', { targetId: created.targetId, flatten: true }, null);
  sessionId = att.sessionId;
  await send('Page.enable', {}, sessionId);
  await send('Runtime.enable', {}, sessionId);
  await send('Log.enable', {}, sessionId);
  /* --auto-hook injects a page-side hook BEFORE any page script runs, so a page
   * that was written by another track can be captured without editing it. The
   * hook wraps configure/getCurrentTexture/submit and installs __x6Readback. */
  if (A.autoHook) {
    const hookPath = resolve(A.autoHook);
    if (!existsSync(hookPath)) { log('--auto-hook file not found: ' + hookPath); process.exit(2); }
    const source = readFileSync(hookPath, 'utf8');
    await send('Page.addScriptToEvaluateOnNewDocument', { source: source }, sessionId);
    log('auto-hook: injected ' + hookPath + ' (' + source.length + ' bytes) before page scripts');
  }
  await send('Emulation.setDeviceMetricsOverride', { width: VW, height: VH, deviceScaleFactor: 1, mobile: false }, sessionId);
  log('navigate ' + A.url);
  await send('Page.navigate', { url: A.url }, sessionId);
  await sleepMs(1500);
} catch (e) {
  log('CDP setup failed: ' + e.message);
  process.exit(3);
}

const evalIn = async (expression, awaitPromise) => {
  const r = await send('Runtime.evaluate', { expression, awaitPromise: !!awaitPromise, returnByValue: true, userGesture: true }, sessionId);
  if (r.exceptionDetails) throw new Error((r.exceptionDetails.exception && r.exceptionDetails.exception.description) || r.exceptionDetails.text);
  return r.result && r.result.value;
};
const q = (x) => JSON.stringify(x);

/* --------------------------------------------------------------- PNG out */
function saveRawPng(raw, w, h, file) {
  const rawFile = file.replace(/\.png$/, '.rgba');
  writeFileSync(rawFile, raw);
  if (A.raw) return file;
  execFileSync('convert', ['-size', w + 'x' + h, '-depth', '8', 'rgba:' + rawFile, file]);
  return file;
}
function imageStats(file) {
  /* Measured proof of what a capture actually contains. Mean alone is not
   * enough: a page with any HTML on it screenshots with a non-zero mean even
   * when the canvas area is fully transparent, which is exactly the headless
   * trap. So the alpha channel is measured separately and the centre pixel is
   * reported verbatim.
   *
   *   mean      average of colour channels (0..1)
   *   meanA     average alpha; ~0 means "nothing was composed"
   *   centre    the pixel at the middle of the image, RGBA
   *   colours   distinct colours
   *   opaque    centre alpha == 1 -- the canvas area is actually present
   */
  const cx = Math.floor(VW / 2), cy = Math.floor(VH / 2);
  try {
    const out = execFileSync('convert', [file, '-format',
      'size=%wx%h mean=%[fx:mean] meanA=%[fx:mean.a] centre=%[pixel:p{' + cx + ',' + cy + '}] colours=%k',
      'info:'], { encoding: 'utf8' }).trim();
    const num = (k) => { const m = new RegExp(k + '=([0-9.eE+-]+)').exec(out); return m ? +m[1] : null; };
    const mean = num('mean'), meanA = num('meanA');
    const centre = (/centre=([^ ]+)/.exec(out) || [])[1] || null;
    const alphaOf = (px) => {
      if (!px) return null;
      const parts = px.replace(/^[a-z]+/i, '').replace(/[()]/g, '').split(',').map((s) => s.trim());
      if (parts.length >= 4) return +parts[3];
      return 1;   /* no alpha channel written => opaque */
    };
    const centreAlpha = alphaOf(centre);
    return { text: out, mean, meanA, centre, centreAlpha,
             nonBlack: Number.isFinite(mean) && mean > 0.002,
             opaque: Number.isFinite(centreAlpha) && centreAlpha > 0.5 };
  } catch (e) { return { text: 'identify failed: ' + e.message, mean: null, meanA: null, centre: null, centreAlpha: null, nonBlack: false, opaque: false }; }
}

/* --------------------------------------------------------------- capture */
const KEYS = { Enter: { key: 'Enter', code: 'Enter', windowsVirtualKeyCode: 13, text: '\r' },
               Escape: { key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 },
               ArrowDown: { key: 'ArrowDown', code: 'ArrowDown', windowsVirtualKeyCode: 40 },
               ArrowUp: { key: 'ArrowUp', code: 'ArrowUp', windowsVirtualKeyCode: 38 },
               ArrowLeft: { key: 'ArrowLeft', code: 'ArrowLeft', windowsVirtualKeyCode: 37 },
               ArrowRight: { key: 'ArrowRight', code: 'ArrowRight', windowsVirtualKeyCode: 39 },
               Space: { key: ' ', code: 'Space', windowsVirtualKeyCode: 32, text: ' ' },
               Tab: { key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 },
               Backspace: { key: 'Backspace', code: 'Backspace', windowsVirtualKeyCode: 8 } };
async function pressKey(name) {
  const k = KEYS[name] || { key: name, code: name, windowsVirtualKeyCode: 0 };
  await send('Input.dispatchKeyEvent', { type: 'keyDown', ...k }, sessionId);
  await send('Input.dispatchKeyEvent', { type: 'keyUp', ...k }, sessionId);
}
async function sendKeys(list) {
  const names = list.split(',').map((s) => s.trim()).filter(Boolean);
  for (const name of names) { await pressKey(name); await sleepMs(250); }
}

const deadline = Date.now() + A.seconds * 1000;
let n = 0, firstReadbackAt = null, pageReady = false, keysSent = false;
const stateExpr = 'JSON.stringify({' +
  'title: document.title,' +
  '"gpu": ("gpu" in navigator),' +
  'canvases: Array.from(document.querySelectorAll("canvas")).map(function(c){return [c.width, c.height, c.id || null];}),' +
  'hook: (typeof window[' + q(A.hook) + '] === "function"),' +
  'body: (document.body ? document.body.innerText : "").slice(0, 400)' +
  '})';

while (Date.now() < deadline) {
  let state = null;
  try { state = await evalIn(stateExpr, false); } catch (e) { log('state evaluate failed: ' + e.message); }
  if (!pageReady && state) { pageReady = true; log('page state ' + String(state).slice(0, 600)); }

  if (!keysSent && A.keys && (Date.now() - t0) / 1000 >= A.keysDelay) {
    keysSent = true;
    log('keys: ' + A.keys);
    await sendKeys(A.keys);
  }

  const hookPresent = !!state && JSON.parse(state).hook;
  if (hookPresent) {
    try {
      const rb = await evalIn('(async () => { const h = window[' + q(A.hook) + ']; const r = await h(); ' +
                              'if (!r || !r.b64) return null; return r; })()', true);
      if (rb && rb.b64) {
        if (firstReadbackAt === null) firstReadbackAt = stamp();
        const idx = String(n).padStart(3, '0');
        const file = join(OUT, 'readback-' + idx + '.png');
        saveRawPng(Buffer.from(rb.b64, 'base64'), rb.w, rb.h, file);
        const st = imageStats(file);
        proof.push({ kind: 'readback', file: file, w: rb.w, h: rb.h, format: rb.format, meta: rb.meta || null,
                     stats: st.text, mean: st.mean, meanA: st.meanA, centre: st.centre, centreAlpha: st.centreAlpha,
                     nonBlack: st.nonBlack, opaque: st.opaque, at: stamp() });
        log('READBACK ' + file + ' ' + rb.w + 'x' + rb.h + ' format=' + rb.format +
            ' meta=' + JSON.stringify(rb.meta || null) + ' | ' + st.text +
            ' | nonBlack=' + st.nonBlack + ' opaque=' + st.opaque);
      } else if (n === 0) {
        log('hook present but returned no pixels yet');
      }
    } catch (e) { log('readback failed: ' + e.message); }
  } else if (n === 0 && state) {
    log('no page hook ' + A.hook + ' yet (page still booting?); readback capture skipped');
  }

  /* Page-side capture of the canvas. For a WebGPU canvas whose drawing buffer is
   * not preserved, toDataURL returns transparent pixels; that outcome is
   * recorded rather than assumed, because it is one of this track's findings. */
  if (!A.noCanvas && state) {
    try {
      const variants = await evalIn('(async () => {' +
        'const out = [];' +
        'const cs = Array.from(document.querySelectorAll("canvas"));' +
        'for (const c of cs) {' +
        '  let data = null, err = null, twoD = null;' +
        '  try { data = c.toDataURL("image/png"); } catch (e) { err = String(e && e.message); }' +
        '  try { twoD = !!c.getContext("2d"); } catch (e) { twoD = false; }' +
        '  out.push({ id: c.id || null, w: c.width, h: c.height, data: data, err: err, twoD: twoD });' +
        '}' +
        'return out; })()', true);
      if (variants && variants.length) {
        const idx = String(n).padStart(3, '0');
        for (let k = 0; k < variants.length; k++) {
          const v = variants[k];
          if (!v.data || v.data.indexOf('data:image/png;base64,') !== 0) {
            log('PAGE-CANVAS ' + (v.id || ('#' + k)) + ' toDataURL unusable (err=' + v.err + ', 2d=' + v.twoD + ')');
            proof.push({ kind: 'page-canvas', canvas: v.id || ('#' + k), usable: false, err: v.err, twoD: v.twoD });
            continue;
          }
          const file = join(OUT, 'page-canvas-' + idx + '-' + k + '.png');
          writeFileSync(file, Buffer.from(v.data.split(',')[1], 'base64'));
          const st = imageStats(file);
          proof.push({ kind: 'page-canvas', file: file, canvas: v.id || ('#' + k), stats: st.text, mean: st.mean, nonBlack: st.nonBlack });
          log('PAGE-CANVAS ' + file + ' canvas=' + (v.id || ('#' + k)) + ' | ' + st.text + ' | nonBlack=' + st.nonBlack);
        }
      }
    } catch (e) { log('page-canvas capture failed: ' + e.message); }
  }

  /* Compositor capture. With a display this is the honest "what the screen
   * shows"; headless it is the trap documented in PIEGE-HEADLESS.md. */
  if (!A.noCompositor) {
    try {
      const shot = await send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: false }, sessionId);
      const idx = String(n).padStart(3, '0');
      const file = join(OUT, 'compositor-' + idx + '.png');
      writeFileSync(file, Buffer.from(shot.data, 'base64'));
      const st = imageStats(file);
      proof.push({ kind: 'compositor', file: file, stats: st.text, mean: st.mean, meanA: st.meanA,
                   centre: st.centre, centreAlpha: st.centreAlpha, nonBlack: st.nonBlack, opaque: st.opaque });
      log('COMPOSITOR ' + file + ' | ' + st.text + ' | centreAlpha=' + st.centreAlpha + ' opaque=' + st.opaque +
          ' | compositor-canvas-visible=' + st.opaque);
    } catch (e) { log('compositor capture failed: ' + e.message); }
  }

  n++;
  await sleepMs(A.interval * 1000);
}

let finalState = null;
try {
  finalState = await evalIn('JSON.stringify({' +
    'title: document.title,' +
    'hook: (typeof window[' + q(A.hook) + '] === "function"),' +
    'g: (typeof window.__g === "function" ? window.__g() : null),' +
    'body: (document.body ? document.body.innerText : "").slice(0, 600)})', false);
} catch (e) { log('final state failed: ' + e.message); }
log('final page state ' + String(finalState).slice(0, 1200));

writeFileSync(join(OUT, 'proof.json'), JSON.stringify({
  url: A.url, headless: !!A.headless, display: A.display || null, size: A.size,
  chrome: CHROME, startedAt: new Date(t0).toISOString(), seconds: A.seconds,
  firstReadbackAt: firstReadbackAt, captures: proof, pageLogs: pageLogs.slice(-80), finalState: finalState
}, null, 1));

const readbacks = proof.filter((p) => p.kind === 'readback');
const compositor = proof.filter((p) => p.kind === 'compositor');
log('summary: readbacks=' + readbacks.length + ' opaqueReadbacks=' + readbacks.filter((p) => p.nonBlack && p.opaque).length +
    ' compositor=' + compositor.length + ' opaqueCompositor=' + compositor.filter((p) => p.opaque).length +
    ' out=' + OUT);
if (!readbacks.length) { log('FAIL: no GPU readback was produced (no page hook, or hook error)'); cleanup(); process.exit(4); }
cleanup();
process.exit(0);
