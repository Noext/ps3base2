/* Every Vulkan format and blend factor the renderer uses must have a decision.
 *
 * The port's riskiest unknowns are the ones WebGPU cannot express: 16-bit packed
 * colour and constant-alpha blending. A table that quietly omitted them would let
 * the renderer be written against a format set that does not exist, and the
 * failure would surface as wrong colours after the port, not as a compile error
 * now. This test requires each one to be PRESENT and CLASSIFIED.
 */
#include "runtime/wgpu_map.h"

#include <cstdio>
#include <cstring>
#include <set>

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what) {
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL: %s\n", what);
    }
}

/* The exact set measured from runtime/src/gpu: 28 VK_FORMAT_* entries. */
static const char* kExpectedFormats[] = {
    "VK_FORMAT_UNDEFINED", "VK_FORMAT_R8_UNORM", "VK_FORMAT_R8G8_UNORM", "VK_FORMAT_R8G8_SNORM",
    "VK_FORMAT_R8G8B8A8_UNORM", "VK_FORMAT_R8G8B8A8_SRGB", "VK_FORMAT_B8G8R8A8_UNORM", "VK_FORMAT_B8G8R8A8_SRGB",
    "VK_FORMAT_R16_UNORM", "VK_FORMAT_R16G16_UNORM", "VK_FORMAT_R16G16_SFLOAT", "VK_FORMAT_R16G16B16A16_SFLOAT",
    "VK_FORMAT_R32_SFLOAT", "VK_FORMAT_R32G32B32A32_SFLOAT", "VK_FORMAT_D16_UNORM", "VK_FORMAT_D32_SFLOAT",
    "VK_FORMAT_D24_UNORM_S8_UINT", "VK_FORMAT_D32_SFLOAT_S8_UINT",
    "VK_FORMAT_A1R5G5B5_UNORM_PACK16", "VK_FORMAT_R5G5B5A1_UNORM_PACK16", "VK_FORMAT_R4G4B4A4_UNORM_PACK16",
    "VK_FORMAT_R5G6B5_UNORM_PACK16",
    "VK_FORMAT_BC1_RGBA_UNORM_BLOCK", "VK_FORMAT_BC1_RGBA_SRGB_BLOCK", "VK_FORMAT_BC2_UNORM_BLOCK",
    "VK_FORMAT_BC2_SRGB_BLOCK", "VK_FORMAT_BC3_UNORM_BLOCK", "VK_FORMAT_BC3_SRGB_BLOCK",
};

int main() {
    size_t n = 0;
    const gpu::wgpu::FormatMap* f = gpu::wgpu::format_table(n);

    check(n >= 28, "at least the 28 measured formats are mapped");

    /* Every expected format is present. */
    int missing = 0;
    for (const char* want : kExpectedFormats) {
        bool found = false;
        for (size_t i = 0; i < n; i++) {
            if (std::strcmp(f[i].vk, want) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            missing++;
            std::printf("  missing: %s\n", want);
        }
    }
    check(missing == 0, "every Vulkan format used by the renderer has an entry");

    /* No entry claims a target it cannot have. */
    int nameless = 0;
    for (size_t i = 0; i < n; i++)
        if (!f[i].wgpu || !f[i].wgpu[0]) nameless++;
    check(nameless == 0, "every entry names a target format");

    /* The 16-bit packed formats must be EXPAND, not silently Direct: this is the
     * expensive one and it must be visible. */
    int expand = 0;
    for (size_t i = 0; i < n; i++)
        if (std::strstr(f[i].vk, "PACK16") && f[i].kind == gpu::wgpu::Map::Expand) expand++;
    check(expand == 4, "the four packed 16-bit formats are classified Expand");

    /* Compressed formats must be Feature-gated, never plain Direct. */
    int bc = 0, bc_feature = 0;
    for (size_t i = 0; i < n; i++) {
        if (!std::strstr(f[i].vk, "_BC")) continue;
        bc++;
        if (f[i].kind == gpu::wgpu::Map::Feature) bc_feature++;
    }
    check(bc == bc_feature && bc > 0, "compressed formats are feature-gated");

    /* Blend table: constant-alpha must not be claimed as direct. */
    size_t bn = 0;
    const gpu::wgpu::BlendMap* b = gpu::wgpu::blend_table(bn);
    check(bn >= 15, "the 15 blend factors used are mapped");
    int ca = 0, ca_emu = 0;
    for (size_t i = 0; i < bn; i++) {
        if (!std::strstr(b[i].vk, "CONSTANT_ALPHA")) continue;
        ca++;
        if (b[i].kind == gpu::wgpu::Map::Emulate) ca_emu++;
    }
    check(ca == 2 && ca_emu == 2, "constant-alpha factors are classified Emulate, not Direct");

    /* Duplicate detection: a table with two rows for one Vk enum is a bug. */
    std::set<const char*> seen;
    int dup = 0;
    for (size_t i = 0; i < n; i++)
        if (!seen.insert(f[i].vk).second) dup++;
    check(dup == 0, "no Vulkan format appears twice");

    /* State beyond formats: every value the renderer names must be classified,
     * and the two WebGPU cannot express must not be claimed as direct. */
    size_t sn = 0;
    const gpu::wgpu::StateMap* st = gpu::wgpu::state_table(sn);
    check(sn >= 26, "the 26 state values used are mapped");

    int fan = 0, fan_emulate = 0;
    for (size_t i = 0; i < sn; i++) {
        if (std::strstr(st[i].vk, "TRIANGLE_FAN")) {
            fan++;
            if (st[i].kind == gpu::wgpu::Map::Emulate) fan_emulate++;
        }
    }
    check(fan == 1 && fan_emulate == 1, "triangle fan is Emulate: WebGPU has no fan topology");

    int cull_all = 0, cull_emulate = 0;
    for (size_t i = 0; i < sn; i++) {
        if (std::strstr(st[i].vk, "FRONT_AND_BACK")) {
            cull_all++;
            if (st[i].kind == gpu::wgpu::Map::Emulate) cull_emulate++;
        }
    }
    check(cull_all == 1 && cull_emulate == 1, "cull FRONT_AND_BACK is Emulate, not absent");

    auto sc = gpu::wgpu::state_coverage();
    std::printf("state  : %d direct, %d feature, %d expand, %d emulate, %d absent\n", sc.direct, sc.feature, sc.expand, sc.emulate, sc.absent);

    auto fc = gpu::wgpu::format_coverage();
    auto bcov = gpu::wgpu::blend_coverage();
    std::printf("formats: %d direct, %d feature, %d expand, %d emulate, %d absent\n", fc.direct, fc.feature, fc.expand, fc.emulate, fc.absent);
    std::printf("blend  : %d direct, %d feature, %d expand, %d emulate, %d absent\n", bcov.direct, bcov.feature, bcov.expand, bcov.emulate, bcov.absent);
    std::printf("wgpu_map: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
