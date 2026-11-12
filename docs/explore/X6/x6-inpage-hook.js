/* x6-inpage-hook.js -- injected into ANY page by tools/x-capture.mjs --auto-hook.
 *
 * It installs window.__x6Readback() without the page's cooperation, which is what
 * makes the harness usable on pages other tracks already wrote (web/game.html,
 * Piste G's gpu-clear.html) without editing their sources.
 *
 * How it works, in the order the page executes:
 *
 *   1. configure() is wrapped so the canvas is given COPY_SRC in addition to
 *      whatever usage the page asked for. Without COPY_SRC a canvas texture
 *      cannot be copied at all (measured twice in this workspace: the readback
 *      then returns 0,0,0,0).
 *   2. getCurrentTexture() is wrapped to remember the texture the page will draw
 *      into. The canvas texture stops being valid at the next
 *      getCurrentTexture() call and is lost at presentation, so it must be
 *      copied in the same task (measured by track G: a later copy returns zeros).
 *   3. queue.submit() is wrapped: after the page's command buffer is queued, one
 *      extra encoder copies the remembered canvas texture into an offscreen
 *      texture created with COPY_SRC, and that offscreen texture is what
 *      __x6Readback() reads.
 *
 * The copy is issued AFTER the page's own submit on the same queue, so it sees
 * the page's render pass output, not the previous frame.
 *
 * Nothing is drawn and no page state is changed: if the page draws nothing, the
 * readback is the canvas clear colour, which is still reported honestly.
 */
(function () {
  if (window.__x6Readback) return;                  /* a page hook wins */
  if (!('gpu' in navigator) || typeof GPUCanvasContext === 'undefined') return;

  var st = { device: null, ctx: null, canvas: null, format: 'bgra8unorm', off: null,
             lastTexture: null, copied: 0, submitted: 0, frames: 0, error: null };

  function ensureOffscreen() {
    if (!st.device || !st.canvas) return null;
    if (st.off && st.off.width === st.canvas.width && st.off.height === st.canvas.height) return st.off;
    if (st.off) st.off.destroy();
    st.off = st.device.createTexture({
      size: [st.canvas.width, st.canvas.height], format: st.format,
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST,
    });
    return st.off;
  }

  var origConfigure = GPUCanvasContext.prototype.configure;
  GPUCanvasContext.prototype.configure = function (desc) {
    try {
      st.ctx = this;
      st.canvas = this.canvas;
      st.device = desc.device;
      st.format = desc.format || 'bgra8unorm';
      var d = Object.assign({}, desc);
      d.usage = (desc.usage === undefined ? GPUTextureUsage.RENDER_ATTACHMENT : desc.usage) | GPUTextureUsage.COPY_SRC;
      origConfigure.call(this, d);
      return;
    } catch (e) { st.error = 'configure wrap: ' + e.message; }
    return origConfigure.call(this, desc);
  };

  var origGetCurrentTexture = GPUCanvasContext.prototype.getCurrentTexture;
  GPUCanvasContext.prototype.getCurrentTexture = function () {
    var t = origGetCurrentTexture.call(this);
    if (this === st.ctx) st.lastTexture = t;
    return t;
  };

  var origSubmit = GPUQueue.prototype.submit;
  GPUQueue.prototype.submit = function (buffers) {
    var r = origSubmit.call(this, buffers);
    st.submitted++;
    if (st.ctx && st.lastTexture && st.device) {
      try {
        var off = ensureOffscreen();
        if (off) {
          var enc = st.device.createCommandEncoder();
          enc.copyTextureToTexture({ texture: st.lastTexture }, { texture: off }, [st.canvas.width, st.canvas.height, 1]);
          origSubmit.call(this, [enc.finish()]);
          st.copied++;
        }
      } catch (e) { st.error = 'copy wrap: ' + e.message; }
    }
    st.lastTexture = null;
    return r;
  };

  window.__x6Readback = async function () {
    var off = ensureOffscreen();
    if (!off || !st.device) return null;
    var w = st.canvas.width, h = st.canvas.height;
    var bytesPerRow = Math.ceil((w * 4) / 256) * 256;
    var buf = st.device.createBuffer({ size: bytesPerRow * h,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    var enc = st.device.createCommandEncoder();
    enc.copyTextureToBuffer({ texture: off }, { buffer: buf, bytesPerRow: bytesPerRow }, [w, h]);
    st.device.queue.submit([enc.finish()]);
    await buf.mapAsync(GPUMapMode.READ);
    var src = new Uint8Array(buf.getMappedRange());
    var dst = new Uint8Array(w * h * 4);
    for (var y = 0; y < h; y++) dst.set(src.subarray(y * bytesPerRow, y * bytesPerRow + w * 4), y * w * 4);
    buf.unmap(); buf.destroy();
    /* Preferred canvas format is often bgra8unorm; PNG needs rgba8. */
    var fmt = String(st.format).toLowerCase();
    if (fmt.indexOf('bgra') === 0) {
      for (var i = 0; i < dst.length; i += 4) { var t = dst[i]; dst[i] = dst[i + 2]; dst[i + 2] = t; }
      fmt = 'rgba' + fmt.slice(4);
    }
    var bin = '';
    for (var j = 0; j < dst.length; j += 0x8000) bin += String.fromCharCode.apply(null, dst.subarray(j, j + 0x8000));
    return { w: w, h: h, format: fmt, b64: btoa(bin),
             meta: { source: 'auto-hook', subs: st.submitted, copies: st.copied,
                     format: st.format, error: st.error } };
  };

  window.__x6HookState = function () {
    return { subs: st.submitted, copies: st.copied, format: st.format, error: st.error,
             canvas: st.canvas ? [st.canvas.width, st.canvas.height] : null };
  };
})();
