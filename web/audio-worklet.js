/* The AudioWorklet that drains the audio ring. Milestone 4.
 *
 * The ring is a SharedArrayBuffer written by the emulation worker and read here
 * on the audio thread. Nothing blocks: when the ring starves we return silence,
 * which is what lets a slow frame not glitch the sound into a buzz.
 *
 * Layout matches runtime/include/runtime/audio_ring.h: interleaved stereo f32,
 * two control words before the samples.
 *   [0] head (frame index, producer)   [1] tail (frame index, consumer)
 *   [2] capacity in frames
 *   [3..] samples
 */

class Ps3Audio extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = (options && options.processorOptions) || {};
    this.ring = o.ring ? new Int32Array(o.ring) : null;
    this.samples = this.ring ? new Float32Array(o.ring, 3 * 4) : null;
    this.capacity = this.ring ? this.ring[2] : 0;
    this.starved = 0;
    this.consumed = 0;
    if (o.statsPort) {
      this.statsPort = o.statsPort;
      this.statsPort.onmessage = () => {
        this.statsPort.postMessage({ starved: this.starved, consumed: this.consumed });
      };
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    if (!out || !out.length || !this.ring || !this.capacity) return true;

    const frames = out[0].length;
    const head = Atomics.load(this.ring, 0);
    const tail = Atomics.load(this.ring, 1);
    let have = head - tail;
    if (have < 0) have += this.capacity;

    const take = Math.min(frames, have);
    if (take < frames) this.starved++;              /* the emulator is behind */
    this.consumed += take;

    for (let i = 0; i < take; i++) {
      const f = ((tail + i) % this.capacity) * 2;
      const l = this.samples[f] || 0;
      const r = this.samples[f + 1] || 0;
      out[0][i] = l;
      if (out.length > 1) out[1][i] = r;
    }
    /* Silence, not a repeat of the last block: a held sample is a tone. */
    for (let i = take; i < frames; i++) {
      out[0][i] = 0;
      if (out.length > 1) out[1][i] = 0;
    }

    let nt = tail + take;
    if (nt >= this.capacity) nt -= this.capacity;
    Atomics.store(this.ring, 1, nt);

    return true;
  }
}

registerProcessor('ps3-audio', Ps3Audio);
