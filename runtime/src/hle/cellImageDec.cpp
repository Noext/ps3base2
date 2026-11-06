
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
#include "runtime/pngdec_convert.h"

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
    /* Output format requested through cellPngDecSetParameter. The decoder used
     * to write RGBA regardless, which is wrong for a font atlas asked for as
     * one byte per pixel. */
    int32_t want_space = 10;        /* CELL_PNGDEC_RGBA */
    int32_t want_mode = 0;
    uint32_t want_depth = 8;
    int32_t want_alpha_sel = 0;
    uint32_t want_alpha = 0xFF;
    int src_color_type = 6;   /* PNG IHDR colour type, kept from the file bytes */
    int src_channels = 4;
    int32_t want_pack = 0;
};

/* How a CellPngDecColorSpace lays out bytes, and how many components it has. */
struct PngFormat {
    int components = 4;
    int bpp = 4;
    bool alpha = true;
    bool argb = false;
    bool palette = false;
};

inline bool png_space_has_alpha(int32_t space)
{
    return space == 9 /* GRAYSCALE_ALPHA */ || space == 10 /* RGBA */ || space == 20 /* ARGB */;
}

PngFormat png_format(int32_t space, int32_t, uint32_t)
{
    PngFormat f;
    switch (space) {
    case 1:  f.components = 1; f.bpp = 1; f.alpha = false; break;   /* GRAYSCALE */
    case 2:  f.components = 3; f.bpp = 3; f.alpha = false; break;   /* RGB */
    case 4:  f.components = 1; f.bpp = 1; f.alpha = false; f.palette = true; break;
    case 9:  f.components = 2; f.bpp = 2; f.alpha = true; break;    /* GRAYSCALE_ALPHA */
    case 20: f.components = 4; f.bpp = 4; f.alpha = true; f.argb = true; break; /* ARGB */
    case 10:
    default: f.components = 4; f.bpp = 4; f.alpha = true; break;    /* RGBA */
    }
    return f;
}

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
    if (select == 1) {
        /* Buffer source: the PNG lives in guest memory. */
        const uint32_t stream_ptr = vm::read32(src_ea + 20);
        const uint32_t stream_size = vm::read32(src_ea + 24);
        if (!stream_ptr || !stream_size || !vm::range_mapped(stream_ptr, stream_size)) {
            LOG_WARN("cellPngDec", "buffer source 0x%08x size %u is not mapped",
                     stream_ptr, stream_size);
            return false;
        }
        out.assign(vm::ptr(stream_ptr), vm::ptr(stream_ptr) + stream_size);
        return true;
    }
    if (select != 0) {
        LOG_WARN("cellPngDec", "stream source (select %d) is not served", select);
        return false;
    }
    const std::string guest_path = hle::gstr(vm::read32(src_ea + 4));
    const uint64_t offset = vm::read64(src_ea + 8);
    uint32_t size = vm::read32(src_ea + 16);

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
    const uint32_t sub = a32(ctx, 1), in_param = a32(ctx, 2), out_param = a32(ctx, 3);
    ImageHandle* ih = get_image_handle(sub);
    if (!ih || !in_param) { LOG_ERROR("pngdec", "SetParameter: bad sub %u / in 0x%08x", sub, in_param); return ret(ctx, (int32_t)0x80611201); }
    uint32_t w[8]; for (int i = 0; i < 8; i++) w[i] = vm::read32(in_param + 4 * i);
    LOG_INFO("pngdec", "SetParameter RAW in=0x%08x: %08x %08x %08x %08x %08x %08x %08x %08x",
             in_param, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
    const int32_t mode = (int32_t)w[1], space = (int32_t)w[2];
    const uint32_t depth = w[3]; const int32_t pack = (int32_t)w[4], asel = (int32_t)w[5]; const uint32_t calpha = w[6];
    if (!(space == 1 || space == 2 || space == 4 || space == 9 || space == 10 || space == 20)) {
        LOG_ERROR("pngdec", "SetParameter: outputColorSpace %d (0x%x) is not a CellPngDecColorSpace: in_param is not a CellPngDecInParam or the NID is wrong", space, space);
        return ret(ctx, (int32_t)0x80611201);
    }
    ih->want_mode = mode; ih->want_space = space; ih->want_depth = depth;
    ih->want_pack = pack; ih->want_alpha_sel = asel; ih->want_alpha = calpha;
    LOG_INFO("pngdec", "SetParameter: sub %u mode %d space %d depth %u pack %d alphaSel %d colorAlpha 0x%x",
             sub, mode, space, depth, pack, asel, calpha);
    const pngdec::Format fmt = pngdec::format_for(space);
    const int wd = ih->width, ht = ih->height;
    if (out_param) {
        vm::write64(out_param + 0, static_cast<uint64_t>(wd) * fmt.bpp);
        vm::write32(out_param + 8, static_cast<uint32_t>(wd));
        vm::write32(out_param + 12, static_cast<uint32_t>(ht));
        vm::write32(out_param + 16, static_cast<uint32_t>(fmt.components));
        vm::write32(out_param + 20, 8);
        vm::write32(out_param + 24, static_cast<uint32_t>(mode));
        vm::write32(out_param + 28, static_cast<uint32_t>(space));
        vm::write32(out_param + 32, static_cast<uint32_t>(wd) * fmt.bpp * ht);
    }
    ret(ctx, 0);
}

HLE_FUNC(cellPngDec, cellPngDecOpen) {
    const uint32_t handle = vm::read32(a32(ctx, 0));
    const uint32_t main_ea = a32(ctx, 1);
    const uint32_t src_ea = a32(ctx, 2);
    const uint32_t info_ea = a32(ctx, 3);
    (void)info_ea;

    if (!get_image_handle(handle)) return ret(ctx, static_cast<int32_t>(0x80611202));

    std::vector<uint8_t> bytes;
    if (!read_src_bytes(src_ea, bytes)) return ret(ctx, static_cast<int32_t>(0x80611203));

    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                        &w, &h, &n, 4);
    if (!px) {
        LOG_WARN("cellPngDec", "stb_image refused the file: %s", stbi_failure_reason());
        return ret(ctx, static_cast<int32_t>(0x80611204));
    }
    /* A sub-handle is one image. Allocating the state here, per Open, is what
     * keeps two open images from sharing dimensions and pixels: the title opens
     * many sub-streams and interleaves their decodes. */
    ImageHandle* ih = new ImageHandle();
    ih->src_color_type = bytes.size() > 25 ? bytes[25] : 6;   /* 0 gray, 2 rgb, 3 palette, 4 gray+alpha, 6 rgba */
    ih->src_channels = ih->src_color_type == 0 ? 1 : ih->src_color_type == 4 ? 2
                     : ih->src_color_type == 2 ? 3 : ih->src_color_type == 3 ? 1 : 4;
    ih->width = w;
    ih->height = h;
    ih->channels = 4;
    ih->rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    ih->valid = true;
    const uint32_t sub = add_image_handle(ih);
    /* PS3BASE_BAND_DUMP: keep the source PNG exactly as the title supplied it,
     * so an external decoder can serve as the oracle. */
    if (const char* dir = std::getenv("PS3BASE_BAND_DUMP"); dir && w <= 64) {
        char nm[512];
        std::snprintf(nm, sizeof nm, "%s/band_sub%u_%dx%d.png", dir, sub, w, h);
        if (FILE* f = std::fopen(nm, "wb")) {
            std::fwrite(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
            LOG_INFO("pngdec", "  source PNG %s (%zu bytes)", nm, bytes.size());
        }
    }

    /* *stream receives the sub-handle, not the main one. Writing the main handle
     * here made every sub-stream resolve to the same state. */
    if (main_ea) vm::write32(main_ea, sub);
    LOG_INFO("cellPngDec", "Open: handle %u -> sub %u %dx%d, %zu bytes of RGBA, rc 0",
             handle, sub, w, h, ih->rgba.size());
    ret(ctx, 0);
}

HLE_FUNC(cellPngDec, cellPngDecReadHeader) {
    const uint32_t sub = a32(ctx, 1);
    ImageHandle* ih = get_image_handle(sub);
    const uint32_t info = a32(ctx, 2);
    if (!ih || !info) return ret(ctx, (int32_t)0x80611201);
    const int32_t src_space = ih->src_color_type == 0 ? 1 : ih->src_color_type == 4 ? 9
                            : ih->src_color_type == 2 ? 2 : ih->src_color_type == 3 ? 4 : 10;
    vm::write32(info + 0, ih->width);  vm::write32(info + 4, ih->height);
    vm::write32(info + 8, ih->src_channels);          // numComponents
    vm::write32(info + 12, (uint32_t)src_space);      // colorSpace
    vm::write32(info + 16, 8); vm::write32(info + 20, 0); vm::write32(info + 24, 0);
    LOG_INFO("pngdec", "ReadHeader: sub %u %dx%d colorType %d -> numComponents %d colorSpace %d",
             sub, ih->width, ih->height, ih->src_color_type, ih->src_channels, src_space);
    ret(ctx, 0);
}

HLE_FUNC(cellPngDec, cellPngDecDecodeData) {
    /* The stream argument carries the sub-handle written by Open. The value is
     * only dereferenced when it points at mapped memory, so a title that passes
     * something else cannot fault us. */
    /* The stream argument is the sub-handle itself (measured: arg1 == 3, the value
     * Open returned), not a pointer to it. Dangling pointers were read as
     * handles, which is what faulted at guest 0x3. */
    const uint32_t handle = a32(ctx, 1) ? a32(ctx, 1) : vm::read32(a32(ctx, 0));
    const uint32_t stream = a32(ctx, 1);
    const uint32_t data = a32(ctx, 2);
    const uint32_t ctrl = a32(ctx, 3);
    const uint32_t out_info = a32(ctx, 4);

    ImageHandle* ih = get_image_handle(handle);
    /* (1) Entry: the pointer the title handed us and what is there now. */
    {
        char b0[16];
        for (int i = 0; i < 16; i++) b0[i] = (data && vm::range_mapped(data + i, 1)) ? static_cast<char>(vm::ptr(data)[i]) : 0;
        LOG_INFO("pngdec", "D0 entry: h%u data 0x%08x subHandle %u bpl %u | data[0..15] = %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                 handle, data, a32(ctx, 5),
                 (ctrl && vm::range_mapped(ctrl, 8)) ? vm::read32(ctrl + 0) : 0,
                 b0[0],b0[1],b0[2],b0[3],b0[4],b0[5],b0[6],b0[7],
                 b0[8],b0[9],b0[10],b0[11],b0[12],b0[13],b0[14],b0[15]);
    }
    if (!ih || !ih->valid || !data) {
        /* (2) early exit */
        LOG_INFO("pngdec", "D2 EXIT: ih %s valid %s data 0x%08x",
                 ih ? "ok" : "null", (ih && ih->valid) ? "yes" : "no", data);
        if (out_info) vm::write32(out_info + 12, 1);   /* ST_RESET */
        ret(ctx, 0);
        return;
    }

    /* The stored pixels must match the declared geometry; a mismatch means the
     * conversion reads past its source and writes whatever is adjacent. */
    const size_t expect = static_cast<size_t>(ih->width) * ih->height * 4;
    if (ih->rgba.size() != expect) {
        LOG_ERROR("pngdec", "DecodeData: h%u rgba %zu != %zux%zu*4 = %zu", handle,
                  ih->rgba.size(), static_cast<size_t>(ih->width),
                  static_cast<size_t>(ih->height), expect);
        if (out_info) vm::write32(out_info + 12, 1);
        ret(ctx, static_cast<int32_t>(0x80611203));
        return;
    }
    if (ih->want_space == 4) {
        LOG_ERROR("pngdec", "DecodeData: PALETTE output requested but stb_image yields no palette "
                  "indices; cannot satisfy this without a palette-preserving decoder");
        if (out_info) vm::write32(out_info + 12, 1);
        ret(ctx, (int32_t)0x80611201);
        return;
    }
    const PngFormat fmt = png_format(ih->want_space, ih->want_alpha_sel, ih->want_alpha);
    /* outputBytesPerLine is the destination pitch when the title gives one. */
    uint32_t pitch = static_cast<uint32_t>(ih->width) * fmt.bpp;
    if (ctrl && vm::range_mapped(ctrl, 16)) {
        /* CellPngDecDataCtrlParam.outputBytesPerLine is a leading u64. */
        const uint64_t req = vm::read64(ctrl + 0);
        if (req >= pitch && req < 0x1000000) pitch = static_cast<uint32_t>(req);
    }

    if (!vm::range_mapped(data, uint64_t(pitch) * ih->height)) {
        /* (2) early exit */
        LOG_INFO("pngdec", "D2 EXIT: destination 0x%08x not mapped for %ux%u pitch %u",
                 data, ih->width, ih->height, pitch);
        LOG_WARN("pngdec", "DecodeData: destination 0x%08x too small for %ux%u pitch %u",
                 data, ih->width, ih->height, pitch);
        if (out_info) vm::write32(out_info + 12, 1);
        ret(ctx, 0);
        return;
    }

    if (std::getenv("PS3BASE_PNG_BOTH") && ih->width <= 64) {
        /* Dump the stored RGBA and the converted output for the same call, so a
         * conversion error can be told apart from bad source data. */
        static std::atomic<uint32_t> bn{0};
        const uint32_t k = bn.fetch_add(1);
        char na[256];
        std::snprintf(na, sizeof na, "/tmp/pngd/both_%02u_%dx%d_rgba.raw", k, ih->width, ih->height);
        if (FILE* fa = std::fopen(na, "wb")) {
            std::fwrite(ih->rgba.data(), 1, ih->rgba.size(), fa);
            std::fclose(fa);
        }
        LOG_INFO("pngdec", "  both dump %s rgba bytes %zu", na, ih->rgba.size());
    }
    /* (3) Immediately before the conversion: destination host pointer, source
     * pointer, and the first bytes of the source. */
    LOG_INFO("pngdec", "D3 pre-convert: host_dst %p guest 0x%08x pitch %u src %p size %zu | src[0..15] = %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
             static_cast<void*>(vm::ptr(data)), data, pitch,
             static_cast<const void*>(ih->rgba.data()), ih->rgba.size(),
             int(ih->rgba[0]), int(ih->rgba[1]), int(ih->rgba[2]), int(ih->rgba[3]),
             int(ih->rgba[4]), int(ih->rgba[5]), int(ih->rgba[6]), int(ih->rgba[7]),
             int(ih->rgba[8]), int(ih->rgba[9]), int(ih->rgba[10]), int(ih->rgba[11]),
             int(ih->rgba[12]), int(ih->rgba[13]), int(ih->rgba[14]), int(ih->rgba[15]));
    pngdec::convert(ih->rgba.data(), ih->width, ih->height, ih->channels, vm::ptr(data),
                    pitch, ih->want_space, static_cast<uint8_t>(ih->want_alpha));
    /* (4) Immediately after: read back through the guest accessor. */
    {
        char b0[16];
        for (int i = 0; i < 16; i++) b0[i] = vm::range_mapped(data + i, 1) ? static_cast<char>(vm::ptr(data)[i]) : 0;
        LOG_INFO("pngdec", "D4 post-convert: data[0..15] = %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                 b0[0],b0[1],b0[2],b0[3],b0[4],b0[5],b0[6],b0[7],
                 b0[8],b0[9],b0[10],b0[11],b0[12],b0[13],b0[14],b0[15]);
    }

    if (out_info) {
        vm::write32(out_info + 0, 0);                 /* chunkInformation */
        vm::write32(out_info + 4, 0);                 /* numText */
        vm::write32(out_info + 8, 0);                 /* numUnknownChunk */
        vm::write32(out_info + 12, 0);                /* CELL_PNGDEC_DEC_STATUS_FINISH */
    }
    if (const char* dumpdir = std::getenv("PS3BASE_PNG_DUMP")) {
        static std::atomic<uint32_t> dn{0};
        const uint32_t k = dn.fetch_add(1);
        char nm[512];
        std::snprintf(nm, sizeof nm, "%s/png_%02u_%dx%d_space%d.pgm", dumpdir, k,
                      ih->width, ih->height, ih->want_space);
        if (FILE* f = std::fopen(nm, "wb")) {
            /* PGM honours the requested layout: 1 byte for grayscale, 3 for RGB,
             * 4 dropped to RGB for the alpha formats so the image is viewable. */
            if (fmt.components == 1) {
                std::fprintf(f, "P5\n%d %d\n255\n", ih->width, ih->height);
                for (int y = 0; y < ih->height; y++) std::fwrite(vm::ptr(data) + uint64_t(y) * pitch, 1, ih->width, f);
            } else {
                std::fprintf(f, "P6\n%d %d\n255\n", ih->width, ih->height);
                for (int y = 0; y < ih->height; y++) {
                    for (int x = 0; x < ih->width; x++) {
                        const uint8_t* p = vm::ptr(data) + uint64_t(y) * pitch + uint64_t(x) * fmt.bpp;
                        uint8_t rgb[3];
                        if (fmt.argb) { rgb[0] = p[1]; rgb[1] = p[2]; rgb[2] = p[3]; }
                        else { rgb[0] = p[0]; rgb[1] = p[1]; rgb[2] = p[2]; }
                        std::fwrite(rgb, 1, 3, f);
                    }
                }
            }
            std::fclose(f);
            LOG_INFO("pngdec", "  dumped %s", nm);
            if (std::getenv("PS3BASE_PNG_DUMP_ALPHA")) {
                char an[512];
                std::snprintf(an, sizeof an, "%s/png_%02u_%dx%d_alpha.pgm", dumpdir, k,
                              ih->width, ih->height);
                if (FILE* fa = std::fopen(an, "wb")) {
                    std::fprintf(fa, "P5\n%d %d\n255\n", ih->width, ih->height);
                    for (int y = 0; y < ih->height; y++) {
                        for (int x = 0; x < ih->width; x++) {
                            const uint8_t* p = vm::ptr(data) + uint64_t(y) * pitch + uint64_t(x) * fmt.bpp;
                            std::fputc(fmt.argb ? p[0] : (fmt.bpp >= 4 ? p[3] : 255), fa);
                        }
                    }
                    std::fclose(fa);
                }
            }
        }
    }
    if (std::getenv("PS3BASE_PNG_DEST_DUMP") && ih->width <= 64) {
        char nm[256];
        std::snprintf(nm, sizeof nm, "/tmp/pngd/dest_sub%u_%dx%d_space%d.raw",
                      handle, ih->width, ih->height, ih->want_space);
        if (FILE* f = std::fopen(nm, "wb")) {
            std::fwrite(vm::ptr(data), 1, uint64_t(pitch) * ih->height, f);
            std::fclose(f);
        }
    }
    LOG_INFO("pngdec", "DecodeData: h%u %dx%d space %d bpp %d pitch %u dest 0x%08x",
             handle, ih->width, ih->height, ih->want_space, fmt.bpp, pitch, data);
    ret(ctx, 0);
}

HLE_FUNC(cellPngDec, cellPngDecClose) {
    /* Same convention as the other handlers: the stream is the sub-handle. */
    const uint32_t sub = a32(ctx, 1);
    if (sub) release_image_handle(sub);
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
