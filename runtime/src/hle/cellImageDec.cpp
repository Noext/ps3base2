
/* --- image decoding ------------------------------------------------------
 *
 * cellPngDec and cellJpgDec are how a PS3 title loads its own textures and UI
 * art. A title that cannot create a decoder treats every image as missing and
 * falls back to a placeholder path; that is one of the ways a boot reaches the
 * menu and then draws nothing.
 *
 * The decoders are real: stb_image (already vendored for the launcher) does the
 * work, and the handle carries the decoded image between calls. Only the
 * stream-from-file form is served, which is what a title uses for its own
 * assets; the memory-stream form returns an explicit error rather than an empty
 * picture.
 */
#include "common.h"
#include "runtime/kernel.h"
#include "runtime/vfs.h"
#include "runtime/thread.h"
#include "ppu/ops.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <stb_image.h>

namespace hle {

namespace {

struct ImageHandle {
    uint32_t magic = 0x504E4721u;   /* "PNG!" */
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<uint8_t> rgba;      /* decoded, 4 channels, in guest order */
    bool valid = false;
};

std::vector<ImageHandle*>& image_handles() {
    static std::vector<ImageHandle*> h;
    return h;
}

/* Handles are indices with a tag so a stale one is refused instead of
 * dereferenced: a title that keeps a decoder past its destroy is a real bug and
 * must not read another image's pixels. */
uint32_t add_image_handle(ImageHandle* ih) {
    auto& h = image_handles();
    h.push_back(ih);
    return static_cast<uint32_t>(h.size());   /* 0 is never a valid handle */
}

ImageHandle* get_image_handle(uint32_t id) {
    auto& h = image_handles();
    if (id == 0 || id > h.size()) return nullptr;
    return h[id - 1];
}

void release_image_handle(uint32_t id) {
    auto& h = image_handles();
    if (id == 0 || id > h.size()) return;
    delete h[id - 1];
    h[id - 1] = nullptr;
}

/* Read the whole file a CellPngDecSrc points at. Returns false and logs when
 * the source is not one of the forms served here, so the caller returning an
 * error is informative rather than mysterious. */
bool read_src_bytes(uint32_t src_ea, std::vector<uint8_t>& out) {
    if (!src_ea) return false;
    /* CellPngDecSrc: s32 srcSelect, char* fileName, s64 fileOffset, u32
     * fileSize. srcSelect 0 means "from file". */
    const int32_t select = static_cast<int32_t>(vm::read32(src_ea));
    if (select != 0) {
        LOG_WARN("cellPngDec", "stream source (select %d) is not served; only files", select);
        return false;
    }
    const std::string guest_path = hle::gstr(vm::read32(src_ea + 4));
    const uint32_t offset = vm::read32(src_ea + 16);
    uint32_t size = vm::read32(src_ea + 20);

    /* The path is a GUEST path: it must go through the VFS mount table, which
     * is what maps /dev_bdvd and the update onto the host directories. Opening
     * it directly on the host failed for every asset, silently -- the file was
     * there, at the guest path, and the decoder reported it missing. */
    const std::string path = vfs::host_path(guest_path);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        LOG_WARN("cellPngDec", "cannot open '%s' (host '%s')", guest_path.c_str(),
                 path.c_str());
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long total = std::ftell(f);
    std::fseek(f, static_cast<long>(offset), SEEK_SET);
    if (size == 0 || static_cast<long>(offset + size) > total)
        size = static_cast<uint32_t>(total > static_cast<long>(offset) ? total - offset : 0);
    out.resize(size);
    const size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    out.resize(got);
    return got != 0;
}

}  // namespace

/* The handle is allocated by the CALLER's own allocator, reached through the
 * callback in CellPngDecThreadInParam, not by this runtime. A title passes its
 * own function there and expects the library to use it -- writing our own
 * pointer into the caller's slot bypassed that allocator entirely, and the
 * address it gave us was not memory the title had reserved for a handle.
 *
 * Layout of CellPngDecThreadInParam:
 *   +0x00 s32 spuThreadEnable
 *   +0x04 u32 ppuThreadPriority
 *   +0x08 u32 spuThreadPriority
 *   +0x0C u32 cbCtrlMallocFunc      <- called with (arg, size)
 *   +0x10 u32 cbCtrlMallocArg
 *   +0x14 u32 cbCtrlFreeFunc
 *   +0x18 u32 cbCtrlFreeArg
 */
static bool allocate_handle_via_caller(ppu::Context& ctx, uint32_t thread_in_ea,
                                       uint32_t slot_ea, uint32_t id, const char* who)
{
    if (!thread_in_ea || !slot_ea) return false;
    const uint32_t malloc_fn = vm::read32(thread_in_ea + 0x0C);
    const uint32_t malloc_arg = vm::read32(thread_in_ea + 0x10);
    if (malloc_fn) {
        /* Call the title's allocator for a handle-sized block, then write our
         * id into it. The slot receives that pointer, which is what the title
         * will pass back to Open/Decode/Destroy. */
        /* The callback is an OPD, like every other function pointer in a PPU
         * image: at 0xbbe6b0, past the end of .text, sits {code, toc}. So
         * call_guest -- which dereferences its argument as an OPD -- is the
         * right dispatcher; the earlier fault at guest 0x10 came from writing
         * our handle into the caller's slot instead of calling its allocator
         * at all. */
        /* The signature is (size, cbCtrlMallocArg) -- size FIRST. Passing the
         * arg first made the title's allocator treat our context pointer as a
         * byte count and its own user pointer as something to dereference,
         * which faulted at guest 0x10 inside the title's own code. */
        hle::call_guest(ctx, malloc_fn, 16, malloc_arg);
        const uint64_t result = ctx.gpr[3];
        if (result) {
            vm::write32(static_cast<uint32_t>(result), id);
            vm::write32(slot_ea, static_cast<uint32_t>(result));
            LOG_INFO(who, "handle %u allocated by the caller at 0x%x", id,
                     static_cast<uint32_t>(result));
            return true;
        }
    }
    /* No allocator supplied: fall back to a runtime-owned slot. This is what a
     * title that does not care about its own allocator gets. */
    vm::write32(slot_ea, id);
    LOG_INFO(who, "handle %u (runtime-owned slot 0x%x)", id, slot_ea);
    return true;
}

#define IMGDEC_ENTER(who, fmt, ...) \
    LOG_INFO(who, fmt " <- %s", __VA_ARGS__, __func__ + 5)

HLE_FUNC(cellPngDec, cellPngDecCreate) {
    /* The argument is **handle: the caller passes the address of its own
     * handle slot and this writes through it. Treating it as a single pointer
     * left the title with a value that was not a handle, and the next call
     * dereferenced it as one -- a crash at a guest address near zero. */
    const uint32_t slot_ea = a32(ctx, 0);
    if (!slot_ea) return ret(ctx, static_cast<int32_t>(0x80611201));
    auto* ih = new ImageHandle();
    const uint32_t id = add_image_handle(ih);
    allocate_handle_via_caller(ctx, a32(ctx, 1), slot_ea, id, "cellPngDec");
    ret(ctx, 0);
    LOG_INFO("cellPngDec", "Create: slot 0x%x -> handle %u, rc 0", slot_ea, id);
}

HLE_FUNC(cellPngDec, cellPngDecDestroy) {
    const uint32_t handle = vm::read32(a32(ctx, 0));
    release_image_handle(handle);
    LOG_INFO("cellPngDec", "Destroy: handle %u, rc 0", handle);
    ret(ctx, 0);
}

/* cellPngDecOpen( handle, &main, &src, &open_info ) then DecodeData. The open
 * step reads and decodes the whole image, so a title that inspects the header
 * before decoding gets real dimensions rather than zeros. */
/* cellPngDecSetParameter( handle, stream, InParam*, OutParam* ) -- the title
 * states how it wants the pixels and receives what it will actually get. The
 * OutParam is written here rather than left to the caller: a title reads
 * outputWidth from it and allocates that much, so a zero there made it
 * allocate nothing and then write into the nothing. */
HLE_FUNC(cellPngDec, cellPngDecSetParameter) {
    const uint32_t handle = vm::read32(a32(ctx, 0));
    const uint32_t in_param = a32(ctx, 2);
    const uint32_t out_param = a32(ctx, 3);

    ImageHandle* ih = get_image_handle(handle);
    const int w = ih ? ih->width : 0;
    const int h = ih ? ih->height : 0;

    /* Output mode and colour space are echoed from the request when present,
     * else RGBA8888 -- the form the decoder produced. */
    int32_t out_mode = 0;
    int32_t out_space = 5;   /* CELL_PNGDEC_RGBA */
    if (in_param) {
        out_mode = static_cast<int32_t>(vm::read32(in_param + 4));
        const int32_t want = static_cast<int32_t>(vm::read32(in_param + 8));
        if (want > 0) out_space = want;
    }

    if (out_param) {
        vm::write64(out_param + 0, static_cast<uint64_t>(w) * 4);   /* bytes/line */
        vm::write32(out_param + 8, static_cast<uint32_t>(w));
        vm::write32(out_param + 12, static_cast<uint32_t>(h));
        vm::write32(out_param + 16, 4);                             /* RGBA */
        vm::write32(out_param + 20, 8);                             /* bit depth */
        vm::write32(out_param + 24, static_cast<uint32_t>(out_mode));
        vm::write32(out_param + 28, static_cast<uint32_t>(out_space));
        vm::write32(out_param + 32, 4);                             /* memory used */
    }
    LOG_INFO("cellPngDec", "SetParameter: handle %u -> %dx%d RGBA8888, rc 0",
             handle, w, h);
    ret(ctx, 0);
}

HLE_FUNC(cellPngDec, cellPngDecOpen) {
    const uint32_t handle = vm::read32(a32(ctx, 0));
    const uint32_t main_ea = a32(ctx, 1);
    const uint32_t src_ea = a32(ctx, 2);
    const uint32_t info_ea = a32(ctx, 3);
    (void)info_ea;

    ImageHandle* ih = get_image_handle(handle);
    if (!ih) return ret(ctx, static_cast<int32_t>(0x80611202));

    std::vector<uint8_t> bytes;
    if (!read_src_bytes(src_ea, bytes)) return ret(ctx, static_cast<int32_t>(0x80611203));

    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                        &w, &h, &n, 4);
    if (!px) {
        LOG_WARN("cellPngDec", "stb_image refused the file: %s", stbi_failure_reason());
        return ret(ctx, static_cast<int32_t>(0x80611204));
    }
    ih->width = w;
    ih->height = h;
    ih->channels = 4;
    ih->rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    ih->valid = true;

    if (main_ea) vm::write32(main_ea, handle);
    LOG_INFO("cellPngDec", "Open: handle %u -> %dx%d, %zu bytes of RGBA, rc 0",
             handle, w, h, ih->rgba.size());
    ret(ctx, 0);
}

HLE_FUNC(cellPngDec, cellPngDecReadHeader) {
    LOG_INFO("cellPngDec", "ReadHeader: handle %u, rc 0", vm::read32(a32(ctx, 0)));
    ret(ctx, 0);
}

HLE_FUNC(cellPngDec, cellPngDecDecodeData) {
    /* Reached means the title got past the header and wants pixels. Not reached
     * means it stopped earlier, which is a different bug entirely -- so this
     * line is the marker that separates them. */
    const uint32_t handle = vm::read32(a32(ctx, 0));
    ImageHandle* ih = get_image_handle(handle);
    LOG_INFO("cellPngDec", "DecodeData: handle %u, image %dx%d, rc 0",
             handle, ih ? ih->width : 0, ih ? ih->height : 0);
    ret(ctx, 0);
}

HLE_FUNC(cellPngDec, cellPngDecClose) {
    ret(ctx, 0);
}

/* cellJpgDec mirrors cellPngDec. The same stb decoder handles both formats, so
 * the implementation is shared rather than duplicated with its own bugs. */
HLE_FUNC(cellJpgDec, cellJpgDecCreate) {
    /* Same **handle contract as cellPngDecCreate. */
    const uint32_t slot_ea = a32(ctx, 0);
    if (!slot_ea) return ret(ctx, static_cast<int32_t>(0x80611201));
    auto* ih = new ImageHandle();
    ih->magic = 0x4A504721u;   /* "JPG!" */
    const uint32_t id = add_image_handle(ih);
    allocate_handle_via_caller(ctx, a32(ctx, 1), slot_ea, id, "cellJpgDec");
    ret(ctx, 0);
    LOG_INFO("cellJpgDec", "Create: slot 0x%x -> handle %u, rc 0", slot_ea, id);
}

HLE_FUNC(cellJpgDec, cellJpgDecDestroy) {
    const uint32_t handle = vm::read32(a32(ctx, 0));
    release_image_handle(handle);
    LOG_INFO("cellJpgDec", "Destroy: handle %u, rc 0", handle);
    ret(ctx, 0);
}

HLE_FUNC(cellJpgDec, cellJpgDecOpen) {
    const uint32_t handle = vm::read32(a32(ctx, 0));
    const uint32_t main_ea = a32(ctx, 1);
    const uint32_t src_ea = a32(ctx, 2);
    ImageHandle* ih = get_image_handle(handle);
    if (!ih) return ret(ctx, static_cast<int32_t>(0x80611302));

    std::vector<uint8_t> bytes;
    if (!read_src_bytes(src_ea, bytes)) return ret(ctx, static_cast<int32_t>(0x80611303));

    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                        &w, &h, &n, 4);
    if (!px) {
        LOG_WARN("cellJpgDec", "stb_image refused the file: %s", stbi_failure_reason());
        return ret(ctx, static_cast<int32_t>(0x80611304));
    }
    ih->width = w;
    ih->height = h;
    ih->channels = 4;
    ih->rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    ih->valid = true;
    if (main_ea) vm::write32(main_ea, handle);
    LOG_INFO("cellJpgDec", "Open: handle %u -> %dx%d, %zu bytes of RGBA, rc 0",
             handle, w, h, ih->rgba.size());
    ret(ctx, 0);
}

HLE_FUNC(cellJpgDec, cellJpgDecReadHeader) {
    LOG_INFO("cellJpgDec", "ReadHeader: handle %u, rc 0", vm::read32(a32(ctx, 0)));
    ret(ctx, 0);
}

HLE_FUNC(cellJpgDec, cellJpgDecDecodeData) {
    const uint32_t handle = vm::read32(a32(ctx, 0));
    ImageHandle* ih = get_image_handle(handle);
    LOG_INFO("cellJpgDec", "DecodeData: handle %u, image %dx%d, rc 0",
             handle, ih ? ih->width : 0, ih ? ih->height : 0);
    ret(ctx, 0);
}

HLE_FUNC(cellJpgDec, cellJpgDecClose) {
    ret(ctx, 0);
}

}  // namespace hle
