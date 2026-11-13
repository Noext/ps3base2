/* S1 -- the emulation worker.
 *
 * It receives the device and the transferred canvas from the page, then boots the
 * module with both already in place:
 *
 *   - preinitializedWebGPUDevice makes emdawnwebgpu adopt the device, so the
 *     module never requests an adapter (a JS promise cannot resolve while the
 *     wasm occupies the thread -- measured twice, and it is the reason the naive
 *     "let the wasm create its own device" path shows a black canvas forever),
 *   - s1Canvas is the OffscreenCanvas the bridge configures, so the renderer's
 *     colour view IS the canvas the page displays.
 *
 * The guest itself runs with PROXY_TO_PTHREAD, which puts main() on a pthread.
 * That is why web/s1-bridge.js proxies its entries: a pthread's realm has its own
 * EMPTY WebGPU object table, measured as "Cannot read properties of undefined
 * (reading 'createCommandEncoder')" until the calls were routed to this realm.
 */
let mod = null;

function log(text) { postMessage({ type: 'log', text: String(text).slice(0, 400) }); }

/* The page writes pad words into a block the module owns; this publishes the
 * addresses so the page can write them with no copy and no message per poll. */
function window_publish_pad(padAddr, connAddr) {
  postMessage({ type: 'pad-seam', padAddr, connAddr });
}

async function boot(device, canvas, width, height) {
  const url = new URL('./game.js', self.location.href);
  const m = await import(url.href);

  mod = await m.default({
    locateFile: (p) => new URL('./' + p, self.location.href).href,
    print: (t) => log(t),
    printErr: (t) => log(t),
    noExitRuntime: true,
    /* Seen by the port's emscripten_webgpu_get_device(), and by web/s1-bridge.js
     * when it configures the canvas. */
    preinitializedWebGPUDevice: device,
    s1Canvas: canvas,
    s1CanvasWidth: width,
    s1CanvasHeight: height,
    /* The renderer's diagnostics go to the page, not to a console nobody reads. */
    ENV: { PS3BASE_LOG: 'info' },
  });

  log('module booted (device=' + (!!device) + ' canvas=' + (!!canvas) + ')');

  try {
    const padAddr = mod._ps3_pad_words_address();
    const connAddr = mod._ps3_pad_connected_address();
    window_publish_pad(padAddr, connAddr);
  } catch (e) { log('pad seam unavailable: ' + e.message); }

  postMessage({ type: 'ready' });

  /* The ring's own counters, reported so a run is measured rather than described.
   * These come from the producer's exports; the ring is NOT drained here any
   * more: the device and the canvas are on the module's side of the boundary, so
   * the renderer draws directly instead of the page replaying records. */
  setInterval(() => {
    try {
      if (typeof mod._rsx_ring_stat !== 'function') return;
      const s = {};
      for (let i = 0; i < 19; i++) s[i] = mod._rsx_ring_stat(i) >>> 0;
      s.addr = mod._rsx_ring_address() >>> 0;
      s.cap = mod._rsx_ring_capacity() >>> 0;
      try { s.draws_renderer = mod._r3_draws(); s.skipped = mod._r3_skipped();
            s.n_method = mod._r3_n_method_get(); } catch (e) {}
      postMessage({ type: 'stats', s });
    } catch (e) { /* the module may be mid-growth */ }
  }, 3000);
}

addEventListener('message', (e) => {
  const m = e.data || {};
  if (m.type !== 'start') return;
  boot(m.device, m.canvas, m.width, m.height).catch((err) =>
    log('boot failed: ' + (err && err.stack || err)));
});
