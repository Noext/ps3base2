/* S2 A/B: the producer ALONE, no consumer, exactly the configuration that measured
 * refused=1160458 on the base. Answers "does the ring still overflow when nobody
 * drains it", which is the duplication question rather than the drain question.
 * usage: node tools/s2_nodrain_run.mjs <outdir> [run_ms]
 */
const OUT = process.argv[2];
const RUN_MS = Number(process.argv[3] || 55000);
process.chdir(OUT);
const logs = [];
const push = (t) => logs.push(String(t));
let M = null, lastGood = null;
const T0 = Date.now();
function snap(why) {
  const s = (i) => { try { return M._rsx_ring_stat(i) >>> 0; } catch (e) { return -1; } };
  let used = -1; try { used = M._rsx_ring_control(0) >>> 0; } catch (e) {}
  const o = { why, t:((Date.now()-T0)/1000).toFixed(1), records:s(0), bytes:s(1),
    stateMethods:s(2), vtxBytes:s(4), idxBytes:s(5), texBytes:s(6), texObjects:s(7),
    shaderBytes:s(8), shaderObjects:s(9), draws:s(10), clears:s(11), flips:s(12),
    refused:s(13), refusedBytes:s(14), geomUnmapped:s(15), texRefused:s(16), threads:s(17),
    texUnchanged:s(18), refs:s(19), distinctObjects:s(20), bytesSaved:s(21),
    shaderAlternations:s(22), used, capacity:(()=>{try{return M._rsx_ring_capacity()>>>0}catch(e){return -1}})() };
  console.log('SNAP ' + JSON.stringify(o));
  lastGood = o;
}
process.on('uncaughtException', (e) => { console.log('UNCAUGHT ' + e.message); try { snap('crash'); } catch (x) {} process.exit(3); });
const mod = await import('file://' + OUT + '/game.js');
M = await mod.default({ print: push, printErr: push, noExitRuntime: true, locateFile: (p) => OUT + '/' + p });
console.log('BOOT ' + (Date.now()-T0) + 'ms');
const t0 = Date.now();
while (Date.now() - t0 < RUN_MS) { await new Promise(r=>setTimeout(r,1000)); const now=(Date.now()-t0)/1000; if(now%5<1.1) { try{snap('sample');}catch(e){} } }
snap('done');
process.exit(0);
