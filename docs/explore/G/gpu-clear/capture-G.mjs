// PISTE G -- capture harness. Chromium (real WebGPU adapter) opens the page; the
// guest's decoded state drives the render pass; the pass output is read back from
// the GPU (copyTextureToBuffer) and written as a PNG.
//
// page.screenshot() is NOT used for the frame: in this headless GPU mode it returns
// every canvas as black (measured -- a plain 2D canvas filled blue screenshots
// black). It is still saved once, as compositor.png, to show that.
//
// usage: node capture-G.mjs <url> <outdir> [durationSeconds]
import { chromium } from 'playwright';
import { mkdirSync, writeFileSync, appendFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';

const URL_ = process.argv[2];
const OUT = process.argv[3] || '/tmp/G/shotsG';
const DURATION = Number(process.argv[4] || 45);
const EXE = process.env.CHROME || '/root/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome';

mkdirSync(OUT, { recursive: true });
const transcript = join(OUT, 'console.log');
writeFileSync(transcript, '');

const args = ['--no-sandbox', '--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-angle=vulkan'];
const browser = await chromium.launch({ executablePath: EXE, headless: true, args });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(2) + 's';
const record = (l) => { const s = '[' + stamp() + '] ' + l; appendFileSync(transcript, s + '\n'); console.log(s); };

page.on('console', (m) => record('console.' + m.type() + ' ' + m.text().slice(0, 400)));
page.on('pageerror', (e) => record('pageerror ' + String(e.message).slice(0, 400)));

record('goto ' + URL_);
await page.goto(URL_, { waitUntil: 'domcontentloaded', timeout: 300000 });
record("'gpu' in navigator === " + await page.evaluate(() => 'gpu' in navigator));

function writePng(raw, w, h, file) {
  /* ImageMagick turns the raw RGBA readback into a PNG; PIL is not assumed. */
  const rawFile = file.replace(/\.png$/, '.rgba');
  writeFileSync(rawFile, raw);
  execFileSync('convert', ['-size', w + 'x' + h, '-depth', '8', 'rgba:' + rawFile, file]);
}

let shot = 0, firstDraw = null;
const deadline = Date.now() + DURATION * 1000;
let last = Date.now(), compositor = false;
while (Date.now() < deadline) {
  const g = await page.evaluate(() => (window.__g ? window.__g() : null));
  if (g && g.frames > 0 && firstDraw === null) {
    firstDraw = stamp();
    record('FIRST GUEST-DRIVEN RENDER frames=' + g.frames + ' guestValue=0x' + (g.lastValue >>> 0).toString(16) +
           ' adapter=' + JSON.stringify(g.adapter));
  }
  if (g && g.frames > 0 && Date.now() - last >= 3000) {
    last = Date.now();
    const rb = await page.evaluate(() => (window.__readback ? window.__readback() : null));
    if (rb && rb.b64) {
      const file = join(OUT, 'gpu-' + String(shot).padStart(3, '0') + '.png');
      writePng(Buffer.from(rb.b64, 'base64'), rb.w, rb.h, file);
      record('READBACK ' + file + ' ' + rb.w + 'x' + rb.h + ' format=' + rb.format +
             ' guestValue=0x' + rb.guestValue.toString(16) + ' labelSeq=' + rb.labelSeq +
             ' clearValue=(' + rb.clearValue.r + ',' + rb.clearValue.g + ',' + rb.clearValue.b + ',' + rb.clearValue.a + ')');
      shot++;
    }
    if (!compositor) {
      compositor = true;
      await page.screenshot({ path: join(OUT, 'compositor-screenshot.png') });
      record('compositor screenshot saved (expected black canvas: it cannot see GPU/canvas content here)');
    }
  }
  await page.waitForTimeout(300);
}

const state = await page.evaluate(() => ({
  g: window.__g ? window.__g() : null,
  hud: (document.getElementById('hud') || {}).textContent,
}));
record('page state ' + JSON.stringify(state, null, 1).slice(0, 3000));
await browser.close();
record('closed after ' + stamp() + ', readbacks=' + shot + ', firstDraw=' + firstDraw);