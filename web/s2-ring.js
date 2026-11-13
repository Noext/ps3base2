/* S2 -- the ring's consumer, used by the Node harness and by the page.
 *
 * Layout numbers come from runtime/include/gpu/rsx_ring.h: CommandRing's control
 * block is 64-byte aligned with head/tail/used first, so the record area starts
 * at ringBase + 0x100. The page views the worker's linear memory directly; this
 * class only ever does the two atomics CommandRing::advance does.
 */
export const KIND = { STATE:1, BUFFER:2, TEXTURE:3, SHADER:4, DRAW:5, CLEAR:6, FLIP:7, REF:8 };
/* CommandRing is CONSTRUCTED ON the buffer: mem_ points at the buffer itself, so a
 * record lives at base + offset, and head_/tail_/used_ are separate words inside
 * the C++ object. H4's consumer assumed a 0x100-byte control block inside the
 * buffer; that was wrong (the object is ~0xC0 and lives elsewhere), and reading
 * records at base+0x100 made every record misparse and the tail update corrupt
 * used_. The addresses come from rsx_ring_word() instead. */
const CONTROL_BYTES = 0;

export class Ring {
  constructor(M) {
    this.M = M;
    this.base = M._rsx_ring_address() >>> 0;
    this.capacity = M._rsx_ring_capacity() >>> 0;
    this.head = M._rsx_ring_word(0) >>> 0;
    this.tail = M._rsx_ring_word(1) >>> 0;
    this.used = M._rsx_ring_word(2) >>> 0;
    this.data = this.base + CONTROL_BYTES;
    this.stats = { records:0, bytes:0, state:0, stateBytes:0, vtx:0, idx:0, bufObjects:0,
                   tex:0, texObjects:0, shader:0, shaderObjects:0, draws:0, clears:0, flips:0,
                   refs:0, refBytes:0, padding:0, unknown:0 };
    this.maxUsed = 0;
    this.drawBytes = 0;
    this.hashes = new Map();   /* kind<<32|hash -> times announced (full or ref) */
    this.full = new Map();     /* kind<<32|hash -> times sent with payload */
    this.last = {};
  }
  heap() { const M=this.M; return (M.HEAPU8 && M.HEAPU8.buffer) || (M.wasmMemory && M.wasmMemory.buffer) || null; }
  drain(maxRecords = 200000) {
    const buf = this.heap(); if (!buf) return 0;
    const mem = new Uint32Array(buf);
    let n = 0;
    for (; n < maxRecords; n++) {
      const used = Atomics.load(mem, this.used >>> 2);
      if (used > this.maxUsed) this.maxUsed = used;
      if (used === 0) break;
      const tail = Atomics.load(mem, this.tail >>> 2);
      const at = this.data + tail;
      const size = mem[at >>> 2] >>> 0;
      const kind = mem[(at >>> 2) + 1] >>> 0;
      if (kind === 0xFFFFFFFF) {
        this.stats.padding++;
        let nt = tail + size; if (nt >= this.capacity - CONTROL_BYTES) nt = 0;
        Atomics.store(mem, this.tail >>> 2, nt);
        /* RMW, never a store: the producer is adding to the same word, and a plain
         * store of (used - size) throws away anything it added in between. That is
         * exactly how used_ wrapped past capacity in the first S2 page run. */
        Atomics.sub(mem, this.used >>> 2, size);
        continue;
      }
      if (size < 8 || size > this.capacity) break;
      const payload = at + 8, plen = size - 8;
      this.stats.records++; this.stats.bytes += plen;
      this.decode(kind, payload, plen);
      let nt = tail + size; if (nt >= this.capacity - CONTROL_BYTES) nt = 0;
      Atomics.store(mem, this.tail >>> 2, nt);
      Atomics.sub(mem, this.used >>> 2, size);
    }
    return n;
  }
  note(kind, hash) {
    const k = kind * 4294967296 + hash;
    this.hashes.set(k, (this.hashes.get(k) || 0) + 1);
  }
  decode(kind, at, plen) {
    const w = () => new Uint32Array(this.heap(), at, Math.max(1, plen >> 2));
    switch (kind) {
      case KIND.STATE: { const a=w(); this.stats.state += plen>>3; this.stats.stateBytes += plen; break; }
      case KIND.BUFFER: {
        const a=w(); this.stats.bufObjects++;
        if (a[0]===0 || a[0]===2) this.stats.vtx += a[3]; else this.stats.idx += a[3];
        break;
      }
      case KIND.REF: {
        const a=w(); this.stats.refs++; this.stats.refBytes += plen;
        this.note(a[1], a[2]);
        this.last.ref = { type:a[0], slot:a[1], hash:a[2], len:a[3] };
        break;
      }
      case KIND.TEXTURE: { const a=w(); this.stats.texObjects++; this.stats.tex += a[6]; break; }
      case KIND.SHADER: { const a=w(); this.stats.shaderObjects++; this.stats.shader += a[1]*4; break; }
      case KIND.DRAW: { const a=w(); this.stats.draws++; this.last.drawPrimitive=a[0]; this.last.drawCount=a[2]; break; }
      case KIND.CLEAR: { const a=w(); this.stats.clears++; this.last.clearColor=a[1]; break; }
      case KIND.FLIP: this.stats.flips++; break;
      default: this.stats.unknown++; break;
    }
  }
  producerStats() {
    const M = this.M;
    const names = ['records','bytes','stateMethods','stateBytes','vtxBytes','idxBytes','texBytes',
                   'texObjects','shaderBytes','shaderObjects','draws','clears','flips','refused',
                   'refusedBytes','geomUnmapped','texRefused','producerThreads','texUnchanged',
                   'refs','distinctObjects','bytesSaved','shaderAlternations'];
    const out = {};
    for (let i=0;i<names.length;i++) { try { out[names[i]] = M._rsx_ring_stat(i) >>> 0; } catch(e) { out[names[i]] = null; } }
    out.used = M._rsx_ring_control(0)>>>0; out.pushed = M._rsx_ring_control(1)>>>0;
    out.popped = M._rsx_ring_control(2)>>>0; out.wrapped = M._rsx_ring_control(3)>>>0;
    return out;
  }
}
