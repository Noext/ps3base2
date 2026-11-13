/* S2 run harness: boot the module under Node, drain the ring from THIS thread the
 * way the page does, and report the producer's and the consumer's numbers.
 *
 * usage: node tools/s2_ring_run.mjs <outdir> [run_ms]
 */
import { Ring } from '../web/s2-ring.js';

const OUT = process.argv[2] || '/tmp/out-S2';
const RUN_MS = Number(process.argv[3] || 45000);
process.chdir(OUT);
const logs = [];
const push = (t) => logs.push(String(t));
let M = null, ring = null, peakT = 0;
const T0 = Date.now();

function report(why) {
  const s = (i) => { try { return M._rsx_ring_stat(i) >>> 0; } catch (e) { return -1; } };
  const out = {
    why,
    producer: { records:s(0), bytes:s(1), stateMethods:s(2), stateBytes:s(3), vtxBytes:s(4),
                idxBytes:s(5), texBytes:s(6), texObjects:s(7), shaderBytes:s(8),
                shaderObjects:s(9), draws:s(10), clears:s(11), flips:s(12), refused:s(13),
                refusedBytes:s(14), geomUnmapped:s(15), texRefused:s(16), threads:s(17),
                texUnchanged:s(18), refs:s(19), distinctObjects:s(20), bytesSaved:s(21),
                shaderAlternations:s(22),
                used:M._rsx_ring_control(0), pushed:M._rsx_ring_control(1),
                popped:M._rsx_ring_control(2), wrapped:M._rsx_ring_control(3),
                capacity:M._rsx_ring_capacity() },
    consumer: ring.stats,
    peakUsedBytes: ring.maxUsed, peakAtSeconds: peakT,
    heapBytes: (M.HEAPU8 && M.HEAPU8.length) || 0,
  };
  console.log('RESULT ' + JSON.stringify(out));
}

process.on('uncaughtException', (e) => {
  console.log('UNCAUGHT ' + e.message);
  try { report('crash'); } catch (x) { console.log('report failed ' + x.message); }
  process.exit(3);
});

const mod = await import('file://' + OUT + '/game.js');
M = await mod.default({ print: push, printErr: push, noExitRuntime: true,
                        locateFile: (p) => OUT + '/' + p });
console.log('BOOT ' + (Date.now() - T0) + 'ms exports=' + (typeof M._rsx_ring_stat));
ring = new Ring(M);

const t0 = Date.now();
while (Date.now() - t0 < RUN_MS) {
  ring.drain();
  if (ring.maxUsed > 0) peakT = +((Date.now() - T0) / 1000).toFixed(1);
  await new Promise((r) => setTimeout(r, 20));
}
report('done');
process.exit(0);
