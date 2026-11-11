/* The emulation worker. Milestone 4.
 *
 * The worker runs the wasm module and pumps its own loop. It must NOT block: the
 * browser delivers page messages only when the current task returns, so the loop
 * is driven by an explicit yield between frames rather than by a while(true).
 *
 * The C++ side yields through emscripten_thread_sleep(0), which is verified to
 * pump the worker's message queue WITHOUT Asyncify (measured: EM_ASYNC_JS throws
 * "Asyncify is not defined" here, and -sASYNCIFY breaks wasm-opt on our 50 MB
 * module). That is why this file looks synchronous.
 */

let mod = null;

/* The page owns the pad views; it reaches them through a global the page polls.
 * Kept here as a function so the addresses stay in one place. */
function window_publish_pad(padAddr, connAddr) {
  postMessage({ type: 'pad-seam', padAddr, connAddr });
}

function log(text) {
  postMessage({ type: 'log', text });
}

/* The page writes pad words into a shared block; the module reads them at
 * poll time. The block must outlive the module, so it is allocated here and its
 * address published through the C symbols host.cpp defines. */
const PAD_FRAMES = 8;

async function boot() {
  const url = new URL('./game.js', import.meta.url);

  mod = await import(url.href);
  const Module = await mod.default({
    locateFile: (p) => new URL('./' + p, import.meta.url).href,
    print: (t) => log(t),
    printErr: (t) => log(t),
    /* Emscripten runs main() on a pthread; we wait for the runtime's own
     * "WASM-RUNTIME-UP" line rather than assuming it is synchronous. */
    noExitRuntime: true,
  });

  /* Give JavaScript typed-array views over the C blocks, so the page writes the
   * pad and the worklet reads the ring with no copy in between. */
  try {
    const padAddr = Module._ps3_pad_words_address();
    const connAddr = Module._ps3_pad_connected_address();
    window_publish_pad(padAddr, connAddr);
    log('pad seam wired at 0x' + padAddr.toString(16));
  } catch (e) {
    log('pad seam unavailable in this build: ' + e.message);
  }

  postMessage({ type: 'ready' });
}

addEventListener('message', (e) => {
  const m = e.data;
  if (m.type === 'start') {
    boot().catch((err) => log('boot failed: ' + (err && err.stack || err)));
  }
});
