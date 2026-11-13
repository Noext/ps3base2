/* H4 -- the real hooks behind the ring producer.
 *
 * gpu/rsx_ring.cpp is written against three function pointers so it builds and
 * tests on the host with no guest map and no Vulkan. This file is the only place
 * that binds them to the runtime, which keeps the producer's logic testable and
 * keeps the "does this touch the device?" question answerable by reading one
 * short file.
 *
 * The texture hook reuses runtime/src/gpu/texture_decode.cpp's describe_texture
 * rather than re-deriving RSX format sizes: that decoder is the authority the
 * native renderer uses, and a second table would drift from it exactly the way
 * rsx_iomap.h's comment warns about.
 */
#include "gpu/rsx_ring.h"

#include "runtime/log.h"
#include "runtime/rsx_iomap.h"
#include "gpu/texture_decode.h"
#include "runtime/memory.h"

/* Declared at FILE scope, not inside the file's anonymous namespace: a namespace
 * declared inside an anonymous namespace is a DIFFERENT namespace, so the linker
 * sees a brand-new undefined symbol and the wasm import aborts on the first call.
 * Measured: "RuntimeError: unreachable ... rsxring::(anonymous)::lv2::rsx_io_to_ea"
 * on the very first texture describe. */
namespace lv2 {
uint32_t rsx_io_to_ea(uint32_t io);
uint32_t rsx_io_base();
}

namespace gpu {
namespace rsxring {

namespace {

/* WHICH io table: the LLE one.
 *
 * This build's command processor is lv2/rsx.cpp's FIFO walker, not gcm_fifo: the
 * guest loads libgcm_sys and _cellGcmInitBody is never HLE'd (measured: "rsx:
 * context at 0x50100000 (driver 0x50200000 ...)", not "(hle init)"). So the io
 * table the ring must translate through is lv2's, and bindng gcm_fifo's would
 * produce an identity table that happens to be right for the ring page and wrong
 * for everything else. lv2::rsx_io_to_ea is that authority; the alias below is
 * declared in runtime/rsx_iomap.h's namespace. */
uint32_t hook_io_to_ea(uint32_t io) {
    const uint32_t ea = ::lv2::rsx_io_to_ea(io);
    {
        /* One-shot diagnostic for the geometry miss, kept because it is the
         * measurement that decides whether the fallback below is the right shape:
         * it reports what the io table said and what the guest map says about the
         * same number, side by side. */
        static int n = 0;
        if (n < 4 && io >= 0x01000000u) {
            n++;
            LOG_INFO("rsxring", "io probe: io=0x%08x table_ea=0x%08x mapped=%d page_mapped=%d",
                     io, ea, vm::range_mapped(io, 4) ? 1 : 0, vm::range_mapped(io & ~0xFFFFFu, 4) ? 1 : 0);
        }
    }
    if (ea) return ea;
    /* MAIN-MEMORY FALLBACK, measured necessary.
     *
     * The io table this build installs maps the COMMAND RING page and little else
     * (the guest maps its ring with cellGcmMapEaIoAddress; vertex arrays live in
     * main memory and are never io-mapped). Measured: every vertex array offset
     * came through as io 0x01480000 and translated to ea 0, so all 1220 draws
     * reported a geometry miss.
     *
     * For main memory the GCM io space IS the guest address space (io 0 == the
     * base of main memory), which gcm_fifo's ea_to_io already relies on ("identity:
     * the page's base already matches"). So when the table has no answer, the io
     * offset is tried as a guest EA -- accepted only when the map actually covers
     * it, so a truly bogus offset still counts as a miss. */
    if (io && vm::range_mapped(io, 4)) return io;
    /* Main memory, properly: the io window's base plus the offset. This is the
     * case that actually occurs -- measured, every vertex array offset arrives as
     * io 0x01480000 while the io table holds only the ring page, so the rank guess
     * above answers zero and this one answers 0x41580000. Accepted only when the
     * guest map covers the result, so a bogus offset is still a miss. */
    if (io) {
        const uint32_t base = ::lv2::rsx_io_base();
        if (base && vm::range_mapped(base + io, 4)) return base + io;
    }
    return 0;
}

uint8_t* hook_guest_ptr(uint32_t ea) {
    if (!ea || !vm::range_mapped(ea, 4)) return nullptr;
    return vm::ptr(ea);
}

/* describe_texture wants the nine textual registers and answers the geometry the
 * ring has to carry: width, height, pitch and the source byte count. Registers
 * that are not in TextureRegs (control3, border) are passed as separate words by
 * the producer; only the six the layout needs are used here, in the order
 * rsx_ring.h documents. */
bool hook_describe_texture(const uint32_t regs[9], uint32_t* width, uint32_t* height,
                           uint32_t* pitch, uint32_t* src_size) {
    TextureRegs tr{};
    tr.offset = regs[0];
    tr.format = regs[1];
    tr.address = regs[2];
    tr.control0 = regs[3];
    tr.control1 = regs[4];
    tr.filter = regs[5];
    tr.rect = regs[6];
    tr.control3 = regs[7];
    tr.border = regs[8];
    TextureLayout layout{};
    if (!describe_texture(tr, layout)) return false;
    *width = layout.width;
    *height = layout.height;
    *pitch = layout.width;
    *src_size = layout.source_size;
    return true;
}

}  // namespace

void install_runtime_hooks() {
    Hooks hooks{};
    hooks.io_to_ea = hook_io_to_ea;
    hooks.guest_ptr = hook_guest_ptr;
    hooks.describe_texture = hook_describe_texture;
    install(hooks);
    /* The in-process measurement consumer stays OFF here: a page build must leave
     * the records for the page. A measurement build turns it on with
     * -DPS3BASE_RSX_TEE_BUILD=1, and the page can toggle it through
     * rsx_ring_tee(). */
    set_tee(false);
}

}  // namespace rsxring
}  // namespace gpu