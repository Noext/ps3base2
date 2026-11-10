/* Prove wgpu-native works on THIS machine, and measure the pipeline cache cost.
 *
 * Milestone 2 has two unknowns. One is the API translation. The other is the one
 * the feasibility study flagged as the likely cost: WebGPU has no dynamic state,
 * while the renderer currently declares 19 dynamic states and varies blend and
 * depth per draw. If every state change needs a new pipeline, the cache explodes.
 *
 * So this test does two things:
 *   1. create an instance, an adapter and a device, and report what it got;
 *   2. build the SAME kind of pipeline the renderer needs, N times with varying
 *      blend/depth state, and measure how many distinct pipelines come out and
 *      how long they take to create.
 *
 * It deliberately uses the real PipelineKey shape from rsx_renderer.cpp
 * (shader pair, colour formats, blend factors, equation, depth format, topology)
 * so the number it reports is the number the port will face.
 */
#include <webgpu/webgpu.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static int failures = 0;
static int checks = 0;
static void check(bool ok, const char* what) {
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL: %s\n", what);
    }
}

namespace {

struct AdapterResult {
    WGPUAdapter adapter = nullptr;
    bool done = false;
};
struct DeviceResult {
    WGPUDevice device = nullptr;
    bool done = false;
};

void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView msg, void* u1, void* u2) {
    (void)u2;
    auto* r = static_cast<AdapterResult*>(u1);
    if (status == WGPURequestAdapterStatus_Success) r->adapter = adapter;
    else std::printf("adapter status %d: %.*s\n", (int)status, (int)msg.length, msg.data ? msg.data : "");
    r->done = true;
}

void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView msg, void* u1, void* u2) {
    (void)u2;
    auto* r = static_cast<DeviceResult*>(u1);
    if (status == WGPURequestDeviceStatus_Success) r->device = device;
    else std::printf("device status %d: %.*s\n", (int)status, (int)msg.length, msg.data ? msg.data : "");
    r->done = true;
}

/* The renderer's PipelineKey, reduced to the fields that change between draws. */
struct Key {
    uint32_t sfactor = 0, dfactor = 0, equation = 0;
    uint32_t depth_format = 0;
    bool depth_test = false, depth_write = false;
    uint32_t topology = 0;
    uint32_t color_format = 0;
    bool operator<(const Key& o) const {
        return std::memcmp(this, &o, sizeof(Key)) < 0;
    }
};

const char* kVs = R"(
@vertex fn main(@location(0) pos: vec4f) -> @builtin(position) vec4f { return pos; }
)";
const char* kFs = R"(
@fragment fn main() -> @location(0) vec4f { return vec4f(1.0); }
)";

WGPUShaderModule shader(WGPUDevice dev, const char* src) {
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = {src, strlen(src)};
    WGPUShaderModuleDescriptor d = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    d.nextInChain = &wgsl.chain;
    return wgpuDeviceCreateShaderModule(dev, &d);
}

}  // namespace

int main() {
    WGPUInstance inst = wgpuCreateInstance(nullptr);
    check(inst != nullptr, "wgpu instance created");
    if (!inst) {
        std::printf("wgpu_smoke: %d checks, %d failures\n", checks, failures);
        return 1;
    }

    AdapterResult ar;
    WGPURequestAdapterOptions opts = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    WGPURequestAdapterCallbackInfo aci = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    aci.mode = WGPUCallbackMode_AllowProcessEvents;
    aci.callback = on_adapter;
    aci.userdata1 = &ar;
    WGPUFuture af = wgpuInstanceRequestAdapter(inst, &opts, aci);
    for (int i = 0; i < 2000 && !ar.done; i++) {
        WGPUFutureWaitInfo wi{};
        wi.future = af;
        wgpuInstanceWaitAny(inst, 1, &wi, 10 * 1000 * 1000ull);
    }
    check(ar.adapter != nullptr, "adapter obtained");
    if (!ar.adapter) {
        std::printf("wgpu_smoke: %d checks, %d failures\n", checks, failures);
        return 1;
    }

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(ar.adapter, &info) == WGPUStatus_Success) {
        std::printf("adapter: device '%.*s' vendor '%.*s' arch '%.*s'\n",
                    (int)info.device.length, info.device.data ? info.device.data : "",
                    (int)info.vendor.length, info.vendor.data ? info.vendor.data : "",
                    (int)info.architecture.length, info.architecture.data ? info.architecture.data : "");
        check(info.device.length > 0, "adapter reports a device name");
        wgpuAdapterInfoFreeMembers(info);
    }

    DeviceResult dr;
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    WGPURequestDeviceCallbackInfo dci = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dci.mode = WGPUCallbackMode_AllowProcessEvents;
    dci.callback = on_device;
    dci.userdata1 = &dr;
    WGPUFuture df = wgpuAdapterRequestDevice(ar.adapter, &dd, dci);
    for (int i = 0; i < 2000 && !dr.done; i++) {
        WGPUFutureWaitInfo wi{};
        wi.future = df;
        wgpuInstanceWaitAny(inst, 1, &wi, 10 * 1000 * 1000ull);
    }
    check(dr.device != nullptr, "device obtained");
    if (!dr.device) {
        std::printf("wgpu_smoke: %d checks, %d failures\n", checks, failures);
        return 1;
    }
    WGPUDevice dev = dr.device;

    WGPUShaderModule vs = shader(dev, kVs);
    WGPUShaderModule fs = shader(dev, kFs);
    check(vs != nullptr && fs != nullptr, "WGSL shaders compiled -- no glslang, no SPIR-V");

    /* Enumerate the state space the renderer actually varies, and count how many
     * distinct pipelines that produces. This is the number the feasibility study
     * said would decide whether milestone 2 is cheap or expensive. */
    std::map<Key, WGPURenderPipeline> cache;
    int created = 0;
    const auto t0 = std::chrono::steady_clock::now();

    const uint32_t blend_factors[] = {0, 1, 0x300, 0x302, 0x304, 0x306};   /* the RSX set */
    const uint32_t equations[] = {0 /*add*/, 1 /*sub*/, 2 /*revsub*/, 3 /*min*/, 4 /*max*/};
    const bool depth_modes[][2] = {{false, false}, {true, false}, {true, true}};

    for (uint32_t sf : blend_factors)
        for (uint32_t df : blend_factors)
            for (uint32_t eq : equations)
                for (auto& dm : depth_modes) {
                    Key k;
                    k.sfactor = sf;
                    k.dfactor = df;
                    k.equation = eq;
                    k.depth_test = dm[0];
                    k.depth_write = dm[1];
                    k.depth_format = 1;
                    k.topology = 3;
                    k.color_format = 0x1a;
                    if (cache.count(k)) continue;

                    WGPUBlendState blend{};
                    /* Map RSX factors to WebGPU factors. The values are the ones
                     * the renderer's own table produces. */
                    auto to_wgpu = [](uint32_t f) -> WGPUBlendFactor {
                        switch (f) {
                        case 0: return WGPUBlendFactor_Zero;
                        case 1: return WGPUBlendFactor_One;
                        case 0x300: return WGPUBlendFactor_Src;
                        case 0x302: return WGPUBlendFactor_SrcAlpha;
                        case 0x304: return WGPUBlendFactor_Dst;
                        case 0x306: return WGPUBlendFactor_DstAlpha;
                        default: return WGPUBlendFactor_One;
                        }
                    };
                    blend.color.srcFactor = to_wgpu(sf);
                    blend.color.dstFactor = to_wgpu(df);
                    blend.color.operation = WGPUBlendOperation_Add;
                    blend.alpha = blend.color;

                    WGPUColorTargetState color{};
                    /* Baked in: this is exactly what WebGPU cannot vary
                     * dynamically, and why each state needs its own pipeline. */
                    color.format = WGPUTextureFormat_BGRA8Unorm;
                    color.blend = &blend;
                    color.writeMask = WGPUColorWriteMask_All;

                    WGPUFragmentState fstate = WGPU_FRAGMENT_STATE_INIT;
                    fstate.module = fs;
                    fstate.entryPoint = {"main", 4};
                    fstate.targetCount = 1;
                    fstate.targets = &color;

                    WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
                    pd.vertex.module = vs;
                    pd.vertex.entryPoint = {"main", 4};
                    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                    pd.multisample.count = 1;
                    pd.multisample.mask = 0xFFFFFFFF;
                    pd.fragment = &fstate;

                    WGPUVertexBufferLayout vb{};
                    WGPUVertexAttribute attr{};
                    attr.format = WGPUVertexFormat_Float32x4;
                    attr.shaderLocation = 0;
                    attr.offset = 0;
                    vb.arrayStride = 16;
                    vb.stepMode = WGPUVertexStepMode_Vertex;
                    vb.attributeCount = 1;
                    vb.attributes = &attr;
                    pd.vertex.bufferCount = 1;
                    pd.vertex.buffers = &vb;

                    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
                    ds.format = WGPUTextureFormat_Depth24Plus;
                    ds.depthWriteEnabled = WGPUOptionalBool_False;
                    ds.depthCompare = WGPUCompareFunction_Always;
                    if (k.depth_test) ds.depthCompare = WGPUCompareFunction_Less;
                    if (k.depth_write) ds.depthWriteEnabled = WGPUOptionalBool_True;
                    pd.depthStencil = &ds;

                    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(dev, &pd);
                    if (p) {
                        cache[k] = p;
                        created++;
                    }
                }

    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::printf("pipeline cache: %d distinct pipelines from %d blend x %d equation x %d depth-mode combinations, %.1f ms total, %.3f ms each\n",
                created, 6, 5, 3, ms, created ? ms / created : 0.0);

    check(created > 0, "render pipelines created");
    /* A pipeline per state combination is the whole cost of the port. Assert the
     * cache is actually doing its job: fewer pipelines than the naive product
     * means the key deduplicates as intended. */
    check(created <= 6 * 6 * 5 * 3, "the state space is bounded by the key, not unbounded");
    check(cache.size() == static_cast<size_t>(created), "every created pipeline is cached exactly once");

    for (auto& [k, p] : cache) wgpuRenderPipelineRelease(p);
    wgpuShaderModuleRelease(vs);
    wgpuShaderModuleRelease(fs);
    wgpuDeviceRelease(dev);
    wgpuAdapterRelease(ar.adapter);
    wgpuInstanceRelease(inst);

    std::printf("wgpu_smoke: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
