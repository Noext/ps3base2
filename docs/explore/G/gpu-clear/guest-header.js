/* Piste G -- header evaluated in the SAME scope as game.js, which begins with
 *   var Module = typeof Module != "undefined" ? Module : {};
 * so this object is adopted and its print/printErr become the runtime's sinks. Those
 * sinks are the only JavaScript that runs while the guest's loop holds the main
 * thread (measured: setInterval never fires under node).
 *
 * The value published here is guest state, and the address is the runtime's own:
 * the walker prints "labels=0x01000000 ... label255 mapped=1", so the label window
 * base is scraped from that line and slot 255 is base + 0xff0. The guest's
 * SEMAPHORE_RELEASE packets (NV4097 method 0x1D70, SET_SEMAPHORE_OFFSET 0xff0)
 * write that slot -- measured 3336 times in 55 s, alternating 1 and 2.
 */
var __G_STATE = '/tmp/G/site4/state.json';
var __G_writes = 0, __G_lastWrite = 0, __G_base = 0, __G_prev = null, __G_seq = 0;
var __G_state = { ok: false, t: 0, labelBase: 0, label255: 0, labelSeq: 0, labelHist: [],
                  methods: 0, lastMethod: 0, lastArg: 0, writes: 0, lastLine: '' };

function __g_publish(line) {
  if (typeof Module === 'undefined' || !Module || typeof Module._wb_stat !== 'function') return;
  var txt = String(line || '');
  var b = /labels=0x([0-9a-fA-F]+)/.exec(txt);
  if (b) __G_base = parseInt(b[1], 16) >>> 0;
  var now = Date.now();
  if (now - __G_lastWrite < 20) return;
  __G_lastWrite = now;
  try {
    __G_state.ok = true;
    __G_state.lastLine = txt.slice(0, 160);
    __G_state.methods = Module._wb_stat(1) >>> 0;
    __G_state.lastMethod = Module._wb_stat(5) >>> 0;
    __G_state.lastArg = Module._wb_stat(6) >>> 0;
    if (__G_base) {
      __G_state.labelBase = __G_base;
      var v = Module._wb_peek32((__G_base + 0xff0) >>> 0) >>> 0;
      if (v !== __G_prev) {
        __G_prev = v;
        __G_state.label255 = v;
        __G_state.labelSeq = ++__G_seq;
        __G_state.labelHist.push(v);
        if (__G_state.labelHist.length > 40) __G_state.labelHist.shift();
      }
    }
    __G_state.writes = ++__G_writes;
    require('fs').writeFileSync(__G_STATE, JSON.stringify(__G_state));
  } catch (e) { __G_state.err = String(e && e.message); }
}

var Module = {
  locateFile: function (p) { return '/tmp/G/priv/' + p; },
  noExitRuntime: true,
  print: function (t) { process.stdout.write(t + '\n'); var m = /ps3base-wasm: t=(\d+)s/.exec(t); if (m) __G_state.t = +m[1]; __g_publish(t); },
  printErr: function (t) { process.stderr.write(t + '\n'); var m = /ps3base-wasm: t=(\d+)s/.exec(t); if (m) __G_state.t = +m[1]; __g_publish(t); }
};
