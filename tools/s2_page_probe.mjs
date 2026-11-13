/* Drive web/s2-page.html in Chromium and read window.__s2.stats after N seconds.
 * usage: node tools/s2_page_probe.mjs <baseUrl> <seconds>
 */
import { chromium } from '/root/Dev/ia-web-seller/node_modules/playwright/index.mjs';

const URL_ = process.argv[2];
const SECONDS = Number(process.argv[3] || 60);
const CHROME = process.env.CHROME || '/root/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome';

const browser = await chromium.launch({
  executablePath: CHROME, headless: true,
  args: ['--no-sandbox', '--enable-unsafe-webgpu', '--disable-dev-shm-usage',
         '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
         '--js-flags=--max-old-space-size=4096'],
});
const page = await browser.newPage();
const lines = [];
page.on('console', (m) => lines.push('console.' + m.type() + ' ' + m.text()));
page.on('pageerror', (e) => lines.push('pageerror ' + e.message));
await page.goto(URL_, { waitUntil: 'domcontentloaded' });

const t0 = Date.now();
while (Date.now() - t0 < SECONDS * 1000) {
  await page.waitForTimeout(1000);
  const s = await page.evaluate(() => window.__s2 && window.__s2.stats).catch(() => null);
  if (s) console.log('SAMPLE ' + JSON.stringify(s));
}
const final = await page.evaluate(() => ({ stats: window.__s2.stats, error: window.__s2.error,
  samples: window.__s2.samples, ready: window.__s2.ready,
  tail: (window.__s2.log || []).slice(-6) })).catch((e) => ({ err: e.message }));
console.log('FINAL ' + JSON.stringify(final));
await browser.close();
process.exit(0);
