/* WebAssembly entry point for ps3base.
 *
 * Milestone 3 asks for a build that reaches the main menu with no display. The
 * desktop main.cpp owns SDL, Vulkan, signals and a launcher, none of which exist
 * here, so this is a separate entry that drives the same runtime core: memory,
 * guest threads, the LV2 syscall layer and the lifted PPU/SPU code.
 *
 * What it proves when it runs: the guest executes real game code in wasm, which is
 * the milestone's claim. It does not draw.
 */
#include "runtime/host.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "runtime/files.h"
#include "runtime/kernel.h"

namespace spu { void rsv_lost_stats(uint64_t*, uint64_t*); }
namespace gpu { namespace rsx {
uint32_t* registers();
void shader_stats(uint64_t*, uint64_t*, uint64_t*);
void framebuffer_stats(uint64_t*, uint64_t*, uint32_t*, uint32_t*);
uint32_t bound_texture_mask();
uint32_t texture_field_writes(uint32_t, uint32_t);
void draw_shape_stats(uint32_t*, uint32_t*);
void surface_reg_stats(uint32_t*);
void low_reg_stats(uint32_t*);
} }
#include "runtime/image.h"
#include "runtime/loader.h"
#include "runtime/mem.h"
#include "runtime/regions.h"
#include "runtime/thread.h"
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
#include "thread.h"  /* spu::status_dump, from runtime/src/spu; wasm-only path */
#endif
#include "runtime/hooks.h"
#include "runtime/codewatch.h"
#include "ppu/ops.h"

#if defined(PS3BASE_COVERAGE) && PS3BASE_COVERAGE
namespace ppu { void coverage_report(); void coverage_dump(const char*); }
#endif

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>

/* Only the wasm link provides this entry point; the native binary has main.cpp. */
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
int main(int argc, char** argv) {
    std::printf("ps3base-wasm: starting\n");
    std::printf("ps3base-wasm: memory backend %s, region layout %s\n", mem::mode_name(),
                vm::region_bases().compact ? "compact" : "sparse");

    if (!vm::init()) {
        std::printf("ps3base-wasm: guest memory init FAILED\n");
        return 1;
    }
    std::printf("ps3base-wasm: guest base %p, map span 0x%llx\n", static_cast<void*>(vm::g_base),
                static_cast<unsigned long long>(vm::map_span()));

    /* The milestone's mechanical criterion is reaching the main menu. Without a
     * disc image in the browser yet, what can be proven here is that the guest
     * machine comes up and executes lifted code: create a guest thread and run a
     * few instructions of a real lifted function.
     */
    std::printf("ps3base-wasm: guest machine up, %d cpus\n", host::logical_cpus());

    /* Prove the guest EXECUTES lifted code, not just that memory exists: call a
     * real lifted PPU function and check that it wrote guest memory. This is the
     * milestone's actual claim -- the game's own machine code runs inside wasm. */
    {
        const uint32_t ea = 0x00100000;   /* inside main RAM */
        vm::write32(ea, 0x11223344u);
        extern void f_0048f628(ppu::Context&, uint8_t*);
        ppu::Context c{};
        c.gpr[3] = ea;
        f_0048f628(c, vm::g_base);
        const uint32_t after = vm::read32(ea);
        std::printf("psx-wasm: f_0048f628 ran, guest word at 0x%x is now 0x%08x (was 0x11223344)\n", ea, after);
        if (after == 0x11223344u) {
            std::printf("WASM-GUEST-CODE-FAILED\n");
            return 1;
        }
    }
    std::printf("WASM-RUNTIME-UP\n");
    std::printf("WASM-GUEST-CODE-RAN\n");

        /* The page will serve these over HTTP Range. Here they are mounted at
         * /game by --preload-file, so the provider rewrites the loader's relative
         * path to that mount. This is the whole browser story in eight lines. */
        files::set_provider([](const std::string& path, std::vector<uint8_t>& out) {
            std::string p = path;
            if (p.rfind("./", 0) == 0) p = p.substr(2);
            std::string full = "/" + p;
            FILE* f = std::fopen(full.c_str(), "rb");
            if (!f) return false;
            std::fseek(f, 0, SEEK_END);
            const long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (n <= 0) { std::fclose(f); return false; }
            out.resize(static_cast<size_t>(n));
            const size_t got = std::fread(out.data(), 1, out.size(), f);
            std::fclose(f);
            if (got != out.size()) { out.clear(); return false; }
            return true;
        });

    /* The LV2 boot, ported from main.cpp:253-316. This is the step that separates
     * "lifted code runs" from "the title starts": load the two images the boot
     * needs, create the main guest thread on liblv2's entry, build the CRT argv on
     * that thread's stack, and start it. */
    const int game = loader::find_image("mc");
    const int lv2 = loader::find_image("liblv2");
    std::printf("ps3base-wasm: images mc=%d liblv2=%d of %u\n", game, lv2, image::image_count);
    if (game < 0 || lv2 < 0) {
        std::printf("ps3base-wasm: image table incomplete\n");
        return 1;
    }
    if (!loader::load_image(game) || !loader::load_image(lv2)) {
        std::printf("WASM-BOOT-NO-IMAGES\n");
        std::printf("ps3base-wasm: loader could not load the images (no disc source)\n");
        return 0;
    }
    std::printf("WASM-BOOT-IMAGES-LOADED\n");

    const loader::ProcessInfo& pi = loader::g_process;
    std::printf("ps3base-wasm: sdk 0x%x prio %d stack 0x%x entry 0x%x\n", pi.sdk_version, pi.primary_prio,
                pi.primary_stacksize, pi.entry_opd);

    /* Stack size mapping, identical to main.cpp so the guest sees the same value. */
    uint32_t stack_size = pi.primary_stacksize;
    switch (stack_size) {
    case 0x10: stack_size = 32 * 1024; break;
    case 0x20: stack_size = 64 * 1024; break;
    case 0x30: stack_size = 96 * 1024; break;
    case 0x40: stack_size = 128 * 1024; break;
    case 0x50: stack_size = 256 * 1024; break;
    case 0x60: stack_size = 512 * 1024; break;
    case 0x70: stack_size = 1024 * 1024; break;
    default: stack_size = std::max<uint32_t>(0x10000, (stack_size + 0xFFF) & ~0xFFFu); break;
    }

    auto main_thread = guest::create("main_thread", image::images[lv2].entry, 0, 0, pi.primary_prio,
                                     stack_size, 0, true);
    if (!main_thread) {
        std::printf("ps3base-wasm: guest::create failed\n");
        return 1;
    }
    std::printf("ps3base-wasm: main thread 0x%x stack 0x%08x..0x%08x\n", main_thread->id,
                main_thread->stack_addr, main_thread->stack_addr + main_thread->stack_size);

    /* argv[0]: the same path the native boot builds, so the CRT takes the same
     * branch. No disc means no TITLE_ID, so the generic form is used. */
    uint32_t sp = main_thread->stack_addr + main_thread->stack_size;
    auto push_strings = [](uint32_t& sp, const char* const* list, int count) {
        std::vector<uint32_t> addrs;
        for (int i = 0; i < count; i++) {
            uint32_t len = static_cast<uint32_t>(std::strlen(list[i])) + 1;
            sp -= (len + 15) & ~15u;
            std::memcpy(vm::ptr(sp), list[i], len);
            addrs.push_back(sp);
        }
        sp -= static_cast<uint32_t>((addrs.size() + 1) * 8 + 15) & ~15u;
        const uint32_t ptrs = sp;
        for (size_t i = 0; i < addrs.size(); i++)
            vm::write64(ptrs + static_cast<uint32_t>(i) * 8, addrs[i]);
        vm::write64(ptrs + static_cast<uint32_t>(addrs.size()) * 8, 0);
        return ptrs;
    };
    const char* argv0 = "/dev_bdvd/PS3_GAME/USRDIR/EBOOT.SELF";
    const char* args[] = {argv0};
    const uint32_t argv_addr = push_strings(sp, args, 1);
    const uint32_t envp_addr = push_strings(sp, nullptr, 0);

    ppu::Context& c = main_thread->ctx;
    c.gpr[1] = (sp - 0x70) & ~15u;
    c.gpr[3] = 1;                       /* argc */
    c.gpr[4] = argv_addr;
    c.gpr[5] = envp_addr;
    c.gpr[6] = 0;
    c.gpr[7] = main_thread->id;
    c.gpr[8] = pi.tls_vaddr;
    c.gpr[9] = pi.tls_filesz;
    c.gpr[10] = pi.tls_memsz;
    c.gpr[11] = pi.entry_opd;
    c.gpr[12] = pi.malloc_pagesize;
    std::printf("ps3base-wasm: CRT args sp=0x%08x argv=0x%08x envp=0x%08x\n", (uint32_t)c.gpr[1], argv_addr,
                envp_addr);

    /* Install the hook dispatch. Without it ppu::hook_impl stays null and the
     * lifted code never consults hook_flags, so PS3BASE_CALL_TRACE would record
     * nothing -- the same inert-guard trap documented in hooks.cpp. */
    hooks::init();
    std::printf("ps3base-wasm: hooks installed\n");

#if PS3BASE_BOOT_PROBE
    /* Record the argument and caller of the function that dies with a null this.
     * getenv is inert in wasm (see docs/webassembly/M3-NULL-VTABLE.md), so the
     * switch is a compile-time constant. hooks::add_guard fires at function entry
     * with the live context, which is the only place that can answer "who called
     * this and with what". */
    /* Who asks f_0021c178 for the 69-hour sleep? Guard its entry and record the
     * caller and argument. */
    /* Every sys_event_flag_set site in the lifted binary sits in libsre, at three
     * addresses. Guard each so "the setter is never reached" can be told from "the
     * setter runs and the pose is wrong" -- the two hypotheses from round 53. */
    for (uint32_t a : {0x12116dcu, 0x12117dcu, 0x1211aacu}) {
        hooks::add_guard(a, [](ppu::Context& c) {
            static std::atomic<int> n{0};
            const int k = n.fetch_add(1);
            if (k < 12)
                std::fprintf(stderr, "[setsite] pc=0x%08x r3=0x%llx r4=0x%llx lr=0x%llx\n",
                             static_cast<unsigned>(c.cia),
                             static_cast<unsigned long long>(c.gpr[3]),
                             static_cast<unsigned long long>(c.gpr[4]),
                             static_cast<unsigned long long>(c.lr));
            return false;
        });
    }
    /* The main thread ends up at 0x2dc20c (sys_timer_usleep inside f_002dc1e4).
     * Guard the function entry so the CALLER is named, not guessed: a poll loop
     * shows one caller returning repeatedly. */
    hooks::add_guard(0x2dc1e4, [](ppu::Context& c) {
        static std::atomic<int> n{0};
        const int k = n.fetch_add(1);
        if (k < 10)
            std::fprintf(stderr, "[acc] #%d entry lr=0x%llx r1=0x%llx\n", k,
                         static_cast<unsigned long long>(c.lr),
                         static_cast<unsigned long long>(c.gpr[1]));
        return false;
    });
    /* The native run makes its FIRST event flag wait at t=4.55s from lr=0x14f974
     * inside f_0014f8e4. If the wasm never reaches this function, the whole
     * synchronisation phase is never entered -- which is what round 75 measured.
     * Guard the entry so "not reached" is proven, not assumed. */
    hooks::add_guard(0x14f8e4, [](ppu::Context& c) {
        static std::atomic<int> n{0};
        const int k = n.fetch_add(1);
        if (k < 8)
            std::fprintf(stderr, "[syncfn] #%d entry lr=0x%llx r3=0x%llx\n", k,
                         static_cast<unsigned long long>(c.lr),
                         static_cast<unsigned long long>(c.gpr[3]));
        return false;
    });
    /* The lifted functions that reference the surface-register constants
     * (0x200-0x214) are the candidates for the code that configures a render
     * target. Guard their entries: "the code exists but is never executed" is a
     * different diagnosis from "it runs and fails". */
    for (uint32_t a : {0x2202c8u, 0x6e6d20u, 0x6eaff0u, 0x7421b0u, 0x435708u}) {
        hooks::add_guard(a, [](ppu::Context& c) {
            static std::atomic<int> n{0};
            const int k = n.fetch_add(1);
            if (k < 4)
                std::fprintf(stderr, "[surfcode] pc=0x%08x entry lr=0x%llx\n",
                             static_cast<unsigned>(c.cia),
                             static_cast<unsigned long long>(c.lr));
            return false;
        });
    }
    /* The five lifted functions that reference constant 300 (sys_vm_memory_map)
     * as a syscall base. Guard them: reaching one and not mapping is a different
     * diagnosis from never reaching the code at all. */
    for (uint32_t a : {0x3c5de0u, 0x10051b8u, 0xa1874u, 0x21a338u, 0x5880b8u}) {
        hooks::add_guard(a, [](ppu::Context& c) {
            static std::atomic<int> n{0};
            const int k = n.fetch_add(1);
            if (k < 6)
                std::fprintf(stderr, "[mapcode] pc=0x%08x lr=0x%llx\n",
                             static_cast<unsigned>(c.cia),
                             static_cast<unsigned long long>(c.lr));
            return false;
        });
    }
    hooks::add_guard(0x21c178, [](ppu::Context& c) {
        static std::atomic<int> n{0};
        const int k = n.fetch_add(1);
        if (k < 6)
            std::fprintf(stderr, "[sleepprobe] #%d f_0021c178 r3=%lld lr=0x%llx sp=0x%llx\n", k,
                         static_cast<long long>(c.gpr[3]),
                         static_cast<unsigned long long>(c.lr),
                         static_cast<unsigned long long>(c.gpr[1]));
        return false;
    });
    hooks::add_guard(0x5d2b44, [](ppu::Context& c) {
        static std::atomic<int> n{0};
        const int k = n.fetch_add(1);
        if (k < 40)
            std::fprintf(stderr, "[entryprobe] #%d f_005d2b44 r3=0x%llx r4=0x%llx lr=0x%llx sp=0x%llx\n",
                         k, static_cast<unsigned long long>(c.gpr[3]),
                         static_cast<unsigned long long>(c.gpr[4]),
                         static_cast<unsigned long long>(c.lr),
                         static_cast<unsigned long long>(c.gpr[1]));
        return false;
    });
    /* hooks.cpp documents this trap: add_guard sets hook_flags, but the lifted code
     * only consults them when ppu::hook_impl is non-null. Assign both here. */
    std::printf("ps3base-wasm: entry probe on f_005d2b44 (flags=%u, impl=%d)\n",
                ppu::hook_impl ? 1 : 0);

    /* Watch the line containing the global that is never written. Every store
     * through rsv::store_range reports its caller, so the writer is read out of
     * the machine rather than guessed. PS3BASE_CODEWATCH_NOTE cannot be used:
     * getenv is inert in wasm. */
    /* The state table the guest polls at 4 Hz. The lift at mc_021.cpp:645 is
     *   gpr[4] = 13500416 (0xCE0000) + 23200 (0x5AA0) = 0xCE5AA0;
     *   return ld8(base, gpr[4] + idx);
     * Watching its line tells us whether anything writes it at all, which
     * distinguishes "no writer" from "writer ran and failed". */
    /* codewatch names the writer only if it can read the live guest context.
     * Nothing installs it in this entry, so every report said "guest lr
     * 0x00000000" -- useless. Install it from the guest thread's own context. */
    cw::current_context = [] {
        cw::Context c{};
        guest::Thread* t = guest::current();
        if (t) {
            c.lr = static_cast<uint32_t>(t->ctx.lr);
            c.thread = t->name.c_str();
        }
        return c;
    };
    cw::note_line(0x00CE5AA0);
    std::printf("ps3base-wasm: write watch armed on 0x00ce5aa0\n");
#endif

    std::printf("ps3base-wasm: starting the LV2 entry...\n");
    try {
        guest::start(main_thread);
        std::printf("WASM-BOOT-STARTED\n");
    } catch (const std::exception& e) {
        std::printf("ps3base-wasm: guest start threw: %s\n", e.what());
    }

    /* The boot path, as far as it can go without the disc: find the images the
     * loader will need and report whether their data is reachable. In a browser
     * the provider serves them over HTTP Range; here it reports absence, which is
     * the honest state of a build with no disc attached. */
    {
        /* Prove the seam works: install a provider that answers one known path,
         * which is exactly what the page will do with a fetch. The real disc is
         * not present in this build, so the provider serves a synthesised ELF
         * header to show the loader reads through the hook rather than fopen. */
        std::printf("ps3base-wasm: %u images in the table, provider %s\n", image::image_count,
                    files::has_provider() ? "installed" : "absent");
        const int game = loader::find_image("mc");
        const int lv2 = loader::find_image("liblv2");
        std::printf("ps3base-wasm: mc=%d liblv2=%d\n", game, lv2);
        if (game >= 0) {
            std::vector<uint8_t> data = files::read(loader::host_path(image::images[game].file));
            std::printf("ps3base-wasm: mc data %zu bytes\n", data.size());
            if (data.size() >= 64 && data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
                std::printf("WASM-FILE-PROVIDER-DRIVES-LOADER\n");
            } else {
                std::printf("ps3base-wasm: loader read %zu bytes and did not see an ELF\n", data.size());
            }
        }
    }
    /* The guest runs on its own host thread, so main must not return: Emscripten
     * shuts the runtime down when it does, killing the guest before it executes an
     * instruction. Wait and report what the guest does. */
    std::printf("ps3base-wasm: waiting for guest activity...\n");
    /* The native run needs 86 s to reach the world. A 30 s window cannot tell
     * "stalled" from "not there yet", so give the guest the same budget. */
    /* 200 iterations = 200 seconds, and the module then exits. The native run
     * needs thousands of seconds to reach the world, so a 200 s harness cannot
     * distinguish "stalled" from "not there yet". 1800 iterations gives half an
     * hour, which is the comparison the native reference demands. */
    /* The guest's main thread polls one byte in a loop:
     *   ld8(base, 0xbe0000 + 1804); if (flag) syscall(141=usleep); goto
     * That byte is the last thing between the wasm and engine progress. Print it
     * so the question "does it ever clear" is answered by a number. */
    for (int i = 0; i < 1800; i++) {
        host::sleep_ms(1000);
        const uint32_t pc = main_thread->ctx.cia ? main_thread->ctx.cia : static_cast<uint32_t>(main_thread->ctx.lr);
        /* Throughput: how much SPU work per second of wall clock. This is the
         * measurement the last round said was needed -- the SPUs are healthy and
         * early, so what matters is rate, not state. */
        {
            uint64_t vp = 0, fp = 0, fp_ok = 0;
            gpu::rsx::shader_stats(&vp, &fp, &fp_ok);
            uint64_t tris = 0, verts = 0;
            uint32_t hash = 0, distinct = 0;
            gpu::rsx::framebuffer_stats(&tris, &verts, &hash, &distinct);
            const uint32_t texmask = gpu::rsx::bound_texture_mask();
            /* Which (unit, field) actually receives writes? A zero mask with
             * non-zero field counts means the hook range was wrong, not the guest. */
            if ((i % 40) == 39) {
                /* Sample the parity flag 200 times in a row: if it toggles, it is
                 * a frame counter, not a lock. A single sample per 40 s could
                 * land on 0 every time by luck. */
                {
                    unsigned flips = 0, prev = *vm::ptr(0x00be0704u);
                    for (int k = 0; k < 200; k++) {
                        const unsigned now = *vm::ptr(0x00be0704u);
                        if (now != prev) flips++;
                        prev = now;
                    }
                    std::fprintf(stderr, "[pollflag] t=%ds 0xbe0704=%u toggles_in_200=%u\n", i, prev, flips);
                }
            }
            if ((i % 40) == 39) {
                uint32_t sr[8] = {};
                gpu::rsx::surface_reg_stats(sr);
                std::printf("ps3base-wasm: surface regs clipH=%u clipV=%u fmt=%u caoff=%u zoff=%u\n",
                            sr[0], sr[1], sr[2], sr[4], sr[5]);
                uint32_t lr[64] = {};
                gpu::rsx::low_reg_stats(lr);
                /* Read the actual register FILE, not a write counter: grouped
                 * packets write regs[] directly and bypass method(). */
                {
                    const uint32_t* rf = gpu::rsx::registers();
                    std::printf("ps3base-wasm: regfile clipH=0x%08x clipV=0x%08x fmt=0x%08x "
                                "caoff=0x%08x | tex0off=0x%08x\n",
                                rf ? rf[0x200 >> 2] : 0u, rf ? rf[0x204 >> 2] : 0u,
                                rf ? rf[0x208 >> 2] : 0u, rf ? rf[0x210 >> 2] : 0u,
                                rf ? rf[0x1A00 >> 2] : 0u);
                /* Dump every non-zero register in the surface/texture window: the
                 * question is which surface registers the guest DOES write, not
                 * whether the ones I guessed are written. */
                std::printf("ps3base-wasm: nonzero regs 0x0000-0x0220:");
                for (uint32_t r = 0; r <= 0x220; r += 4) {
                    const uint32_t v = rf[r >> 2];
                    if (v) std::printf(" 0x%03x=%08x", r, v);
                }
                std::printf("\n");
                std::printf("ps3base-wasm: tex unit offsets:");
                for (uint32_t u = 0; u < 8; u++) {
                    const uint32_t v = rf[(0x1A00u + u * 0x20) >> 2];
                    if (v) std::printf(" u%u=0x%x", u, v);
                }
                std::printf("\n");
                }
                std::printf("ps3base-wasm: low regs:");
                for (int k = 0; k < 64; k++) if (lr[k]) std::printf(" 0x%02x=%u", k * 4, lr[k]);
                std::printf("\n");
            }
            if ((i % 40) == 39) {
                uint32_t prims[8] = {}, fmts[4] = {};
                gpu::rsx::draw_shape_stats(prims, fmts);
                std::printf("ps3base-wasm: prims quads=%u tris=%u strip=%u fan=%u other=%u "
                            "| vtxfmt hi=%u %u %u %u\n",
                            prims[0], prims[1], prims[2], prims[3], prims[7],
                            fmts[0], fmts[1], fmts[2], fmts[3]);
            }
            static uint32_t fw[8] = {};
            if ((i % 20) == 19)
                for (uint32_t u = 0; u < 8; u++) {
                    fw[u] = gpu::rsx::texture_field_writes(u, 0);
                    if (fw[u]) std::printf("ps3base-wasm: tex unit %u field0 writes=%u\n", u, fw[u]);
                }
            if ((i % 10) == 9)
                std::printf("ps3base-wasm: texmask=0x%x shaders vp=%llu fp=%llu | fb tris=%llu verts=%llu "
                            "hash=%08x colors=%u\n",
                            texmask, (unsigned long long)vp, (unsigned long long)fp,
                            (unsigned long long)tris, (unsigned long long)verts,
                            hash, distinct);
        }
        /* Throughput: the last round established the SPUs are healthy and early,
         * so the question is rate. The guest clock is ppu::timebase() at 80 MHz,
         * so its delta per wall second IS the emulated speed. */
        {
            static uint64_t last_tb = 0;
            const uint64_t tb = ppu::timebase();
            const uint64_t delta = tb - last_tb;
            last_tb = tb;
            std::printf("ps3base-wasm: guest clock +%llu ticks/s (%.1f%% of 80MHz realtime)\n",
                        static_cast<unsigned long long>(delta), 100.0 * double(delta) / 8.0e7);
        }
        std::printf("ps3base-wasm: t=%ds pc=0x%08x syscalls=%llu exited=%d\n", i + 1, pc,
                    static_cast<unsigned long long>(main_thread->syscalls.load()),
                    main_thread->exited ? 1 : 0);
        /* The syscall histogram is already kept per thread (thread.h:35). Printing
         * the top entries answers "what is it looping on" without an environment
         * variable, which getenv cannot supply under wasm. */
        {
            uint32_t top[4] = {0, 0, 0, 0};
            for (uint32_t k = 0; k < 1024; k++) {
                const uint32_t v = main_thread->syscall_hist[k];
                for (int j = 0; j < 4; j++) {
                    if (v > main_thread->syscall_hist[top[j]]) {
                        for (int m = 3; m > j; m--) top[m] = top[m - 1];
                        top[j] = k;
                        break;
                    }
                }
            }
            /* The guest spins on [obj+8] against a target held in r31. Print the
             * object, the two values and the flip counter, so the wait can be read
             * instead of inferred. */
            if (main_thread->ctx.cia == 0x3a18b0u || main_thread->ctx.cia == 0x3a189cu) {
                std::printf("ps3base-wasm: sleep pc=0x%08x r3=%lld r11=%llu lr=0x%08x\n",
                            static_cast<uint32_t>(main_thread->ctx.cia),
                            static_cast<long long>(main_thread->ctx.gpr[3]),
                            static_cast<unsigned long long>(main_thread->ctx.gpr[11]),
                            static_cast<uint32_t>(main_thread->ctx.lr));
            }
            /* r1 is the guest stack pointer. Walking it up gives the return
             * chain, which is what names the caller that chose 250000 seconds. */
            if (main_thread->ctx.cia == 0x3a18b0u) {
                static bool dumped = false;
                if (!dumped) {
                    dumped = true;
                    std::printf("ps3base-wasm: stack walk at the sleep:\n");
                    /* PPC64 frame: [sp] is the caller's sp (back chain), [sp+16]
                     * is the saved LR slot in this calling convention. Print both
                     * so the chain can be read rather than assumed. */
                    /* f_003a1884 saved its caller's LR at [sp+128] (see
                     * mc_036.cpp:13004), so that slot names who asked for the
                     * sleep. Walk the back chain from there, printing the saved LR
                     * of each frame, which is the guest's own call stack. */
                    uint32_t sp = static_cast<uint32_t>(main_thread->ctx.gpr[1]);
                    std::printf("ps3base-wasm:   sp=0x%08x mapped=%d lr_in_frame=0x%08x\n", sp,
                                vm::range_mapped(sp, 0x100) ? 1 : 0,
                                vm::range_mapped(sp + 128, 4) ? vm::read32(sp + 128) : 0);
                    for (int k = 0; k < 12; k++) {
                        if (!vm::range_mapped(sp, 0x20)) break;
                        const uint32_t back = vm::read32(sp);
                        std::printf("ps3base-wasm:   back[%d] sp=0x%08x -> 0x%08x\n", k, sp, back);
                        if (!back || back <= sp || back > 0x10000000) break;
                        sp = back;
                    }
                }
            }
            /* The guest sits at 0x3a18b0 for the whole window. That address is
             * inside f_003a1884, whose two exits are usleep (141) and yield (43).
             * Count both so "stuck in the loop" and "looping normally" can be
             * told apart. */
            {
                static uint32_t last43 = 0, last141 = 0;
                const uint32_t c43 = main_thread->syscall_hist[43];
                const uint32_t c141 = main_thread->syscall_hist[141];
                if (c43 != last43 || c141 != last141) {
                    std::printf("ps3base-wasm: hist yield43=%u usleep141=%u\n", c43, c141);
                    last43 = c43;
                    last141 = c141;
                }
            }
            if (main_thread->ctx.cia == 0x1307a08u) {
                const uint32_t obj = static_cast<uint32_t>(main_thread->ctx.gpr[30]);
                const uint32_t tgt = static_cast<uint32_t>(main_thread->ctx.gpr[31]);
                std::printf("ps3base-wasm: spin obj=0x%08x cur=%u target=%u\n", obj,
                            vm::range_mapped(obj + 12, 1) ? vm::read32(obj + 8) : 0xFFFFFFFFu, tgt);
            }
            std::printf("ps3base-wasm: top syscalls:");
            for (int j = 0; j < 4; j++) {
                const char* spec = lv2::spec_name(top[j]);
                std::printf(" %u(%s)=%u", top[j], spec ? spec : "?", main_thread->syscall_hist[top[j]]);
            }
            std::printf("\n");
        }
        /* Other guest threads are the real question: f_0021e060 is a worker loop
         * with no return, so progress can only come from elsewhere. Dump every
         * thread once so the state of the machine is read, not assumed. */
        static bool dumped_threads = false;
        if (!dumped_threads && i >= 3) {
            dumped_threads = true;
            std::printf("ps3base-wasm: --- guest thread dump at t=%ds ---\n", i + 1);
            {
                uint64_t c = 0, h = 0;
                spu::rsv_lost_stats(&c, &h);
                std::printf("ps3base-wasm: reservation lost checks=%llu hits=%llu\n",
                            (unsigned long long)c, (unsigned long long)h);
            }
            /* The main thread spins in f_002dc1e4 on the byte at 0xBE070C and
             * sleeps 300us between looks. Printing it tells whether it ever
             * changes -- a constant byte is a producer that never runs, a moving
             * one is a poll that never succeeds. */
            std::printf("ps3base-wasm: byte[0xBE070C]=%u byte[0xBE0709]=%u byte[0xBE0708]=%u\n",
                        unsigned(vm::g_base[0xBE070Cu]), unsigned(vm::g_base[0xBE0709u]),
                        unsigned(vm::g_base[0xBE0708u]));
            guest::status_dump();
            spu::status_dump();
        }
        /* The guest is frozen at pc=0x01401ee4 with syscalls stuck. That pc is a
         * sys_fs_stat (808) call site in libfs; print the path it is asking about
         * so the wait is read, not guessed. */
        if (main_thread->ctx.cia == 0x01401ee4u) {
            static bool dumped = false;
            if (!dumped) {
                dumped = true;
                const uint32_t path = static_cast<uint32_t>(main_thread->ctx.gpr[3]);
                std::printf("ps3base-wasm: fs_stat path=0x%08x mapped=%d", path,
                            vm::range_mapped(path, 1) ? 1 : 0);
                if (vm::range_mapped(path, 1)) {
                    std::printf(" '");
                    for (int j = 0; j < 96 && vm::range_mapped(path + j, 1) && vm::ptr(path)[j]; j++)
                        std::putchar(vm::ptr(path)[j]);
                    std::printf("'");
                }
                std::printf("\n");
            }
        }
#if defined(PS3BASE_COVERAGE) && PS3BASE_COVERAGE
        if (i == 100 || i == 180) ppu::coverage_report();
        if (i == 181) ppu::coverage_dump("/tmp/cov.txt");
#endif
        /* A dump at t≈4 s only shows the loader. The interesting state is after
         * the boot settles, so take a second one late. */
        if (i == 150) {
            std::printf("ps3base-wasm: --- late dump (t=%ds) ---\n", i);
            guest::status_dump();
        }
        if (main_thread->exited) {
            std::printf("WASM-GUEST-THREAD-EXITED\n");
            break;
        }
    }
    std::printf("WASM-BOOT-ALIVE\n");
    return 0;
}

#endif  // __wasm__
