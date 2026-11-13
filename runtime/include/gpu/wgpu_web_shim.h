#pragma once

/* The one place the web target and the native target disagree about webgpu.h.
 *
 * Native builds link wgpu-native and include <webgpu/wgpu.h>, whose C API is
 * Dawn's plus a few helpers. The browser build links emdawnwebgpu and includes
 * <webgpu/webgpu.h>; measured over the 46 wgpu* symbols wgpu_backend.cpp calls,
 * exactly ONE is missing there:
 *
 *     $ for s in $(cat /tmp/wgpu-syms.txt); do
 *         grep -q "\\b$s\\b" .../emdawnwebgpu_pkg/webgpu/include/webgpu/webgpu.h \
 *           || echo "MISSING $s"; done
 *     MISSING wgpuDevicePoll
 *
 * wgpuDevicePoll(device, wait, timeout) drives pending callbacks. In the browser
 * there is nothing to drive: emdawnwebgpu resolves its futures through the JS
 * event loop, which the browser pumps itself. Measured directly: a wasm module
 * that cleared a canvas texture and submitted, WITHOUT any poll at all, produced
 * the exact expected pixel (/tmp/S1-probe/shots/page-canvas-000-0.png,
 * centre srgba(30,200,120,1)).
 *
 * It is a no-op rather than absent because the native backend calls it on the
 * error-checking paths, and removing those calls would fork the two targets in
 * the place where a silent difference is most expensive: the error reporting
 * that decides whether a draw was skipped. */
#include <webgpu/webgpu.h>

#if defined(__EMSCRIPTEN__)
/* Signature copied from the native header (third_party/wgpu-native/include/
 * webgpu/wgpu.h:1489), so the call sites compile unchanged on both targets:
 *     WGPUBool wgpuDevicePoll(WGPUDevice, WGPUBool wait, WGPUSubmissionIndex const*); */
typedef const struct WGPUSubmissionIndexImpl* WGPUSubmissionIndex;
static inline WGPUBool wgpuDevicePoll(WGPUDevice, WGPUBool, WGPUSubmissionIndex const*) { return 1; }
#endif
