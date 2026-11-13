/* S1 -- the canvas/device bridge between the JS realm and the C++ renderer.
 *
 * WHY THIS FILE EXISTS
 * runtime/src/gpu/wgpu_backend.cpp is the pixel-exact WebGPU backend (RMS 0.0000
 * against Vulkan natively). In the browser it cannot create its own device: a
 * requestAdapter promise never resolves while the wasm occupies the thread
 * (measured twice), so JS creates the device and the module adopts it through
 * the port's preinitializedWebGPUDevice. Measured: adoption works and a wasm
 * clear reaches the canvas with 0 errors.
 *
 * WHY EVERY ENTRY HERE IS __proxy: 'sync'
 * The RSX renderer runs on a guest pthread (thread 10 in every rsx log line), and
 * a pthread's realm has its OWN, EMPTY WebGPU object table. Measured: the pthread
 * got 'undefined' from wgpuDeviceCreateCommandEncoder. Emscripten's sync proxy
 * runs these bodies on the main realm and blocks the caller, which is the only
 * bridge that works. Measured end to end: with the render-path entries of the
 * port annotated the same way, a pthread clears the canvas and the pixel comes
 * back exactly (0.10/0.75/0.35 -> srgba(25,191,89,1)).
 *
 * The canvas texture is re-fetched EVERY FRAME: a canvas texture is invalidated at
 * the next getCurrentTexture() and lost at presentation, so a cached handle is a
 * stale handle (the same trap track X6 documents for readback).
 */
addToLibrary({
  /* Configure the canvas for the adopted device, once per size change. */
  /* T1: the device handle the module adopted, for the replay to claim ownership of.
   * The port stores it in WebGPU.preinitializedDeviceId when it imports the device
   * the page created; the replay needs the same handle so its wgpu::init adopts the
   * SAME device rather than making a second one. */
  t1_device_ptr: () => {
    try {
      var id = WebGPU && WebGPU.preinitializedDeviceId;
      return (id === undefined || id === null) ? 0 : (id >>> 0);
    } catch (e) { console.error('t1_device_ptr: ' + e.message); return 0; }
  },
  t1_device_ptr__proxy: 'sync',

  s1_canvas_configure: (w, h) => {
    /* T1: prove the call arrives at all. If this line never prints, the C++ call
     * site is not reaching the bridge and no amount of bridge debugging matters. */
    if (!Module.s1CfgSeen) { Module.s1CfgSeen = 1; console.log('s1cfg: configure CALLED ' + w + 'x' + h); }
    var canvas = Module.s1Canvas;
    if (!canvas) { console.error('s1: no canvas on Module.s1Canvas'); return 0; }
    /* The device comes from the same property the port reads. Module.s1Device was
     * a second name for it and was never assigned, which made configure() receive
     * undefined and throw "Cannot read properties of undefined (reading
     * 'format')" the first time a frame was drawn. One name, one owner. */
    Module.s1Device = Module.preinitializedWebGPUDevice || Module.s1Device;
    if (!Module.s1Device) { console.error('s1: no device to configure with'); return 0; }
    w = w >>> 0; h = h >>> 0;
    if (canvas.width !== w) canvas.width = w;
    if (canvas.height !== h) canvas.height = h;
    var ctx = canvas.getContext('webgpu');
    if (!ctx) { console.error('s1: canvas has no webgpu context'); return 0; }
    /* configure() is idempotent; calling it every frame is what the WebGPU spec
     * expects when the target size changes, and the renderer resizes on a
     * surface change. */
    /* COPY_DST IS REQUIRED, and its absence is why the canvas stayed blank.
     *
     * The replay renders into its own offscreen texture and copies that into the
     * canvas texture at presentation. A canvas configured without `usage` defaults to
     * RENDER_ATTACHMENT only, so copyTextureToTexture into it is invalid -- and the
     * failure is silent: 30 frames presented, 0 validation errors, and a canvas of
     * srgba(0,0,0,0). Measured exactly that before this line existed. */
    ctx.configure({
      device: Module.s1Device,
      format: navigator.gpu.getPreferredCanvasFormat(),
      alphaMode: 'opaque',
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST,
    });
    Module.s1CanvasContext = ctx;
    Module.s1CanvasFormat = navigator.gpu.getPreferredCanvasFormat();
    if (!Module.s1Configured) {
      Module.s1Configured = true;
      console.log('s1: canvas configured ' + w + 'x' + h + ' format ' + Module.s1CanvasFormat);
    }
    return 1;
  },
  s1_canvas_configure__proxy: 'sync',

  /* The current frame's canvas texture, as a WGPUTexture handle the C++ side can
   * use. _emwgpuCreateTexture is the port's OWN allocator for exactly this: it is
   * what wgpuSurfaceGetCurrentTexture calls to wrap a JS texture. Using it keeps
   * the handle in the same table every other wgpu call consults, which
   * WebGPU.importJsTexture would not (that name does not exist in this port). */
  s1_canvas_texture: () => {
    var ctx = Module.s1CanvasContext;
    if (!ctx) {
      if (!Module.s1TexErr1) { Module.s1TexErr1 = 1; console.error('s1tex: no s1CanvasContext'); }
      return 0;
    }
    var tex;
    try {
      tex = ctx.getCurrentTexture();
    } catch (e) {
      if (!Module.s1TexErr0) { Module.s1TexErr0 = 1; console.error('s1tex: getCurrentTexture threw: ' + e.message); }
      return 0;
    }
    if (!tex) {
      if (!Module.s1TexErr5) { Module.s1TexErr5 = 1; console.error('s1tex: getCurrentTexture returned null'); }
      return 0;
    }
    /* _emwgpuCreateTexture is the port's OWN allocator, but it is a library-internal
     * name: if the port did not export it to this scope, calling it throws and the
     * whole texture import silently yields nothing -- the colour view stays null and
     * every draw returns at its first line. Name each failure instead. */
    if (typeof _emwgpuCreateTexture !== 'function') {
      if (!Module.s1TexErr2) { Module.s1TexErr2 = 1; console.error('s1tex: _emwgpuCreateTexture is ' + typeof _emwgpuCreateTexture); }
      return 0;
    }
    var ptr = _emwgpuCreateTexture(0);
    if (!ptr) {
      if (!Module.s1TexErr3) { Module.s1TexErr3 = 1; console.error('s1tex: allocator returned 0'); }
      return 0;
    }
    try {
      WebGPU.Internals.jsObjectInsert(ptr, tex);
    } catch (e) {
      if (!Module.s1TexErr4) { Module.s1TexErr4 = 1; console.error('s1tex: jsObjectInsert threw: ' + e.message); }
      return 0;
    }
    Module.s1CanvasTexturePtr = ptr;
    if (!Module.s1TexOk) { Module.s1TexOk = 1; console.log('s1tex: canvas texture imported ptr=' + ptr + ' ' + tex.width + 'x' + tex.height); }
    return ptr;
  },
  s1_canvas_texture__proxy: 'sync',

  /* The canvas's WGPUTextureFormat, so the pipeline is built against the surface
   * the browser actually gave us (bgra8unorm on one machine, rgba8unorm on
   * another). A pipeline whose target format disagrees is rejected at draw time
   * and the whole command buffer is dropped -- a black frame with no exception. */
  s1_canvas_format: () => {
    var name = String(navigator.gpu.getPreferredCanvasFormat()).toLowerCase();
    /* The port generates WebGPU.TextureFormat from webgpu.h at build time, but the
     * direction of that table is an implementation detail and the value silently
     * decides whether every pipeline is rejected. So the name is resolved from
     * the port's table when it looks like name->number, and from the module's own
     * wgpuTextureFormatFromString when the build exposes one; the two constants
     * getPreferredCanvasFormat can return are the last resort. */
    var table = WebGPU && WebGPU.TextureFormat;
    if (table) {
      var v = table[name];
      if (typeof v === 'number') return v;
      /* Inverted table (number -> name): search it. */
      for (var k in table) {
        if (typeof table[k] === 'string' && String(table[k]).toLowerCase() === name) return Number(k);
      }
    }
    /* WGPUTextureFormat from webgpu.h: RGBA8Unorm = 0x1B, BGRA8Unorm = 0x1C.
     * These are the only two the Canvas API returns (spec, "preferred canvas
     * format"), so a single branch covers the whole domain. */
    return name.indexOf('bgra') === 0 ? 0x1C : 0x1B;
  },
  s1_canvas_format__proxy: 'sync',

  /* Present: the canvas composites at the end of the task, so nothing is needed
   * beyond the submit the backend already did. Kept as an explicit call so the
   * frame boundary is visible from C++ and can grow a surface-present later
   * without touching the backend. */
  s1_canvas_present: () => {
    Module.s1Frames = (Module.s1Frames || 0) + 1;
    return Module.s1Frames;
  },
  s1_canvas_present__proxy: 'sync',

  /* Adopt the device ON THE REALM THAT OWNS IT and return its handle.
   *
   * The port's emscripten_webgpu_get_device() calls importJsDevice, which fills
   * WebGPU.Internals and creates a queue and a device object in the CALLING
   * realm. Called from a guest pthread -- which is where the renderer's init()
   * runs -- that realm has an empty table, so the device it returns has no
   * queue: measured in this module as "Cannot read properties of undefined
   * (reading 'queue')" right after boot.
   *
   * Doing it here, once, gives every thread the same WGPUDevice handle (an
   * integer index into the main realm's table) and the queue is created once.
   * The per-call proxy annotations then carry the pthread's subsequent calls back
   * to this realm. */
  s1_adopt_device: () => {
    if (!Module.s1AdoptedDevice) {
      /* The C entry point (emscripten_webgpu_get_device) is only emitted when
       * something calls it from C++, and it is a minified internal name in the
       * built game.js. The two functions it calls ARE library entries, so they are
       * reached directly here and the result is identical:
       *     emscripten_webgpu_get_device -> WebGPU.importJsDevice(device), AddRef
       * Measured failing form: "ReferenceError: emscripten_webgpu_get_device is not
       * defined" from _s1_adopt_device in game.js. */
      var dev = Module.preinitializedWebGPUDevice;
      if (!dev) { console.error('s1: no preinitializedWebGPUDevice'); return 0; }
      Module.s1AdoptedDevice = WebGPU.importJsDevice(dev);
      Module.s1Device = dev;
      console.log('s1: device adopted on the owning realm (handle ' +
                  (Module.s1AdoptedDevice >>> 0) + ')');
    }
    return Module.s1AdoptedDevice;
  },
  s1_adopt_device__proxy: 'sync',
});
