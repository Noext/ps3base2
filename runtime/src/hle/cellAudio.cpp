/* ps3base -- cellAudio (libaudio) HLE, for Red Dead Redemption (BLES00680).
 *
 * The title imports seven functions (out-rdr/ppu/table.cpp:77122-77128); gta-sa
 * imports cellAudioAddData on top of them. The behaviour and every return code
 * below is refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAudio.cpp, named per function.
 * The state machine -- argument order, output structures, port size arithmetic,
 * the read index, the notify events -- lives in audio_hle.h so tests/cellAudio
 * drives exactly the code the import runs.
 *
 * What this module must get right, because the guest acts on it:
 *
 *   - CellAudioPortConfig is 32 bytes with readIndexAddr at 0 and status at 4
 *     (vm pointers are 32-bit, refs/.../vm_ptr.h:20). portSize is the EXACT
 *     size ch*blocks*256*4, and portAddr is the real mapped guest buffer.
 *   - The read index at readIndexAddr is a big-endian u64 that ADVANCES once
 *     per 5.333 ms period while the port is started. A title that polls it to
 *     find the block to fill stalls if it never moves.
 *   - Port start zeroes the whole port buffer, as RPCS3 does (:1850).
 *
 * Deviations, and why they cannot change an answer the console gives:
 *
 *  1. RPCS3 waits (lv2_sleep) inside PortOpen and PortStart for VSH; there is
 *     no VSH here and the wait changes no value the caller can observe, so it is
 *     not reproduced. The handlers are still called on the guest's thread.
 *  2. RPCS3 dereferences guest pointers unchecked. A null or unmapped pointer
 *     there faults the emulator; here it is refused with the code the function
 *     returns for a bad argument, and the deviation is logged.
 *  3. The real mixer (below) mixes into an audio device when one exists. Its
 *     period counter is the state machine's, so the index and the events the
 *     guest sees are the ones the console's mixer produces, device or no device.
 */
#include "common.h"
#include "audio_hle.h"

#include "runtime/loader.h"
#include "runtime/placement.h"
#include "runtime/host.h"
#include "runtime/hostsleep.h"
#include "runtime/session.h"
#include "../lv2/event.h"
#include "../lv2/object.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#pragma comment(lib, "ole32.lib")
#else
#if !defined(__wasm__) && !defined(__EMSCRIPTEN__)
#include <SDL3/SDL.h>
#endif
#endif
#include "runtime/audio_ring.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace hle {

namespace {

using namespace hle::audio;

std::mutex g_lock;
Manager g_mgr;
std::atomic<bool> g_thread_live{false};

/* One period of the audio clock, 256 samples at 48 kHz. This is the number
 * cellAudioGetPortTimestamp multiplies a tag delta by (RPCS3 cellAudio.cpp:1006)
 * and the interval the mixer thread wakes at. */
constexpr uint32_t AUDIO_BLOCK_PERIOD_US = 5333;
static_assert(BLOCK_SAMPLES * 1000000u / 48000u == AUDIO_BLOCK_PERIOD_US,
              "the block period is AUDIO_BLOCK_SAMPLES * 1e6 / sample rate (cellAudio.cpp:1006)");

#ifdef _WIN32
struct Output {
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    UINT32 frames = 0;
    bool ok = false;

    bool open() {
        if (const char* e = std::getenv("GOWA_AUDIO"); e && e[0] == '0') return false;
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return false;
        IMMDeviceEnumerator* en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en)))) return false;
        IMMDevice* dev = nullptr;
        HRESULT hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        en->Release();
        if (FAILED(hr)) return false;
        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
        dev->Release();
        if (FAILED(hr)) return false;
        WAVEFORMATEXTENSIBLE wf{};
        wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wf.Format.nChannels = 2;
        wf.Format.nSamplesPerSec = 48000;
        wf.Format.wBitsPerSample = 32;
        wf.Format.nBlockAlign = 8;
        wf.Format.nAvgBytesPerSec = 48000 * 8;
        wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wf.Samples.wValidBitsPerSample = 32;
        wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        wf.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 1000000, 0,
                                reinterpret_cast<WAVEFORMATEX*>(&wf), nullptr);
        if (FAILED(hr)) {
            LOG_WARN("cellAudio", "WASAPI initialize failed 0x%08lx", static_cast<unsigned long>(hr));
            return false;
        }
        client->GetBufferSize(&frames);
        if (FAILED(client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render)))) return false;
        client->Start();
        ok = true;
        LOG_INFO("cellAudio", "WASAPI output opened, buffer %u frames", frames);
        return true;
    }

    void write(const float* stereo, UINT32 n) {
        if (!ok) return;
        UINT32 pad = 0;
        if (FAILED(client->GetCurrentPadding(&pad))) return;
        UINT32 room = frames > pad ? frames - pad : 0;
        static const UINT32 target = std::getenv("GOWA_AUDIO_LATENCY_MS") ? static_cast<UINT32>(std::atoi(std::getenv("GOWA_AUDIO_LATENCY_MS")) * 48) : 4800;
        if (pad > target) return;
        if (n > room) n = room;
        if (!n) return;
        BYTE* dst = nullptr;
        if (FAILED(render->GetBuffer(n, &dst))) return;
        std::memcpy(dst, stereo, size_t(n) * 8);
        render->ReleaseBuffer(n, 0);
    }
};
#elif defined(__wasm__) || defined(__EMSCRIPTEN__)
/* The browser has no SDL audio device, but it does have an AudioWorklet, and
 * milestone 4 feeds it from shared memory. The HLE side is unchanged -- port
 * allocation, the index area, the block/offset protocol are all device-free --
 * so the only thing that changes is where the samples go.
 *
 * The ring is a fixed block of shared memory handed in by the page; when no page
 * has attached one yet (a headless run under Node), writes are counted and
 * discarded exactly as before, which is why the no-device path still works. */
extern "C" {
/* Filled in by the page before the module starts: pointer to the ring's float
 * storage and its capacity in stereo frames. Zero means headless. */
extern float* g_audio_ring_memory;
extern uint32_t g_audio_ring_frames;
}

struct Output {
    gpu::AudioRing ring{g_audio_ring_memory, g_audio_ring_frames ? g_audio_ring_frames : 1};
    bool attached = false;
    uint64_t underruns = 0;
    uint64_t silent_frames = 0;

    bool open() {
        attached = g_audio_ring_memory != nullptr && g_audio_ring_frames != 0;
        return attached;
    }

    void write(const float* stereo, uint32_t n) {
        if (!attached) { silent_frames += n; return; }
        const uint32_t put = ring.write(stereo, n);
        if (put < n) underruns++;          /* the worklet is behind, or absent */
    }
};
#else
struct Output {
    SDL_AudioStream* stream = nullptr;

    bool open() {
        if (const char* e = std::getenv("GOWA_AUDIO"); e && e[0] == '0') return false;
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            LOG_WARN("cellAudio", "SDL audio init failed: %s", SDL_GetError());
            return false;
        }
        SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream) {
            LOG_WARN("cellAudio", "SDL audio open failed: %s", SDL_GetError());
            return false;
        }
        SDL_ResumeAudioStreamDevice(stream);
        LOG_INFO("cellAudio", "SDL audio output opened (%s)", SDL_GetCurrentAudioDriver());
        return true;
    }

    uint64_t underruns = 0;

    void write(const float* stereo, uint32_t n) {
        if (!stream) return;
        if (SDL_GetAudioStreamQueued(stream) == 0) underruns++;
        static const int target = std::getenv("GOWA_AUDIO_LATENCY_MS") ? std::atoi(std::getenv("GOWA_AUDIO_LATENCY_MS")) * 48 : 4800;
        if (SDL_GetAudioStreamQueued(stream) / 8 > target) return;
        SDL_PutAudioStreamData(stream, stereo, static_cast<int>(n * 8));
    }
};
#endif

/* Guest floats are big-endian IEEE-754, which is what ppu::ldf32 and stf32 use
 * (runtime/src/ppu_ops.cpp). Port buffers are guest memory, so every read and
 * write here goes through the byte swap. */
float be_float(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    v = __builtin_bswap32(v);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

/* Mixes one 256-sample block of a port into the stereo output. The channel map
 * is RPCS3's (:1380-1420): 2ch is L/R, 8ch is L + 0.707*(C + rear L + side L). */
void mix_block(const Port& p, uint64_t block, float* stereo) {
    const uint8_t* src = vm::ptr(p.addr) + size_t(block) * p.num_channels * BLOCK_BYTES_PER_CHANNEL;
    const float c = 0.7071f;
    for (uint32_t s = 0; s < BLOCK_SAMPLES; s++) {
        const uint8_t* f = src + size_t(s) * p.num_channels * 4;
        float l = 0.0f, r = 0.0f;
        if (p.num_channels == 1) {
            l = r = be_float(f);
        } else if (p.num_channels == 2) {
            l = be_float(f);
            r = be_float(f + 4);
        } else {
            float fl = be_float(f), fr = be_float(f + 4), fc = be_float(f + 8);
            float bl = be_float(f + 16), br = be_float(f + 20);
            float sl = p.num_channels >= 8 ? be_float(f + 24) : 0.0f, sr = p.num_channels >= 8 ? be_float(f + 28) : 0.0f;
            l = fl + c * fc + c * bl + c * sl;
            r = fr + c * fc + c * br + c * sr;
        }
        stereo[s * 2] += l;
        stereo[s * 2 + 1] += r;
    }
}

/* N5b probe: PS3BASE_AUDIO_NOTIFY_PROBE registers a real notify queue from the
 * host and counts what audio::step delivers into it, so "one event per block" is
 * a number from the live mixer thread and not a claim about the code. It is off
 * unless asked for; when on it changes only this runtime's own queue state. */
std::atomic<uint64_t> g_probe_delivered{0};
std::atomic<uint64_t> g_probe_failed{0};
std::atomic<uint64_t> g_probe_key{0};

/* The delivery function audio::step calls once per registered notify queue. */
void deliver(uint64_t key, uint64_t source, uint64_t d1, uint64_t d2, uint64_t d3) {
    const bool probe = g_probe_key.load() == key;
    if (!lv2::send_event_by_key(key, source, d1, d2, d3)) {
        if (probe) g_probe_failed.fetch_add(1, std::memory_order_relaxed);
        static std::atomic<int> warned{0};
        if (warned.fetch_add(1) < 4)
            LOG_WARN("cellAudio", "notify event for key 0x%llx went nowhere (the queue was destroyed)",
                     static_cast<unsigned long long>(key));
        return;
    }
    if (probe) g_probe_delivered.fetch_add(1, std::memory_order_relaxed);
}

/* Create a real notify queue under the console's key base and register it with
 * audio::set_notify_event_queue, so the live audio::step delivers into a real
 * lv2 queue that a real receive can drain. Returns the key, 0 on failure. */
uint64_t probe_register_notify_queue() {
    const uint32_t depth = 8;
    for (uint32_t i = 0; i < 64; i++) {
        const uint64_t key = 0x80004d494f323221ull + i;
        if (!lv2::create_queue_by_key(key, depth, "cellAudio notify (N5b probe)")) continue;
        if (audio::set_notify_event_queue(g_mgr, key, 0)) return 0;
        g_probe_key.store(key);
        return key;
    }
    return 0;
}

/* The consumer side of the probe: one thread per probe queue, receiving exactly
 * as a title would, so "the queue delivers and something wakes" is one loop. */
void probe_receiver(uint64_t key) {
    host::set_thread_name("cellAudio probe rx");
    auto q = lv2::queue_by_key(key);
    if (!q) return;
    std::unique_lock lk(q->m);
    while (g_thread_live) {
        if (!lv2::wait_for(lk, q->cv, 20000, [&] { return !q->events.empty() || q->destroyed; })) continue;
        if (q->destroyed) break;
        while (!q->events.empty()) {
            q->events.pop_front();
            q->received++;
        }
    }
}

void audio_thread() {
    host::set_thread_name("cellAudio");
    placement::apply("audio");
    Output out;
    out.open();
    auto next = std::chrono::steady_clock::now();
    const auto period = std::chrono::microseconds(AUDIO_BLOCK_PERIOD_US);
    std::vector<float> stereo(2 * BLOCK_SAMPLES);
    uint64_t late = 0, stale = 0, blocks = 0;
    uint64_t block_hash[PORT_COUNT][32] = {};
    const bool every_second = session::active();
    auto stats_at = std::chrono::steady_clock::now() + std::chrono::seconds(every_second ? 1 : 5);
    while (g_thread_live) {
        next += std::chrono::microseconds(static_cast<int64_t>(period.count() / ppu::time_scale()));
        host::sleep_until_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(next.time_since_epoch()).count());
        auto woke = std::chrono::steady_clock::now();
        if (woke - next > std::chrono::milliseconds(4)) late++;
        if (woke >= stats_at) {
            stats_at = woke + std::chrono::seconds(every_second ? 1 : 5);
#ifdef _WIN32
            uint64_t underruns = 0;
#else
            uint64_t underruns = out.underruns;
            out.underruns = 0;
#endif
            /* Every started port's buffer, measured rather than assumed: how
             * many of its blocks hold a byte the guest wrote. A mixer that runs
             * while the title never fills a block reports 0 here, which is the
             * difference between 'audio is wired up' and 'audio is silent'. */
            char fill[192] = {};
            uint64_t index_writes = 0, index_last = 0;
            {
                std::lock_guard lk(g_lock);
                size_t used = 0;
                for (const Port& mp : g_mgr.ports) {
                    if (mp.state != PortState::started || !mp.addr || !mp.size) continue;
                    if (!vm::range_mapped(mp.addr, mp.size)) continue;
                    const uint32_t block_bytes = mp.num_channels * BLOCK_BYTES_PER_CHANNEL;
                    uint32_t filled = 0;
                    for (uint32_t b = 0; b < mp.num_blocks && (b + 1) * block_bytes <= mp.size; b++) {
                        for (uint32_t i = 0; i < block_bytes; i++) {
                            if (vm::ptr(mp.addr)[b * block_bytes + i]) { filled++; break; }
                        }
                    }
                    if (used < sizeof fill)
                        used += static_cast<size_t>(std::snprintf(fill + used, sizeof fill - used,
                                                                  " port%u %u/%u filled, index %llu",
                                                                  mp.number, filled, mp.num_blocks,
                                                                  static_cast<unsigned long long>(mp.index_last)));
                }
                index_writes = 0;
                index_last = 0;
                if (g_mgr.ports[0].state == PortState::started) {
                    index_writes = g_mgr.ports[0].index_writes;
                    index_last = g_mgr.ports[0].index_last;
                }
            }
            if (every_second || late || stale || underruns)
                LOG_INFO("cellAudio", "audio stats: %llu blocks, %llu late wakeups, %llu stale blocks, %llu device underruns, "
                         "period %llu, port0 index %llu after %llu writes:%s",
                         static_cast<unsigned long long>(blocks), static_cast<unsigned long long>(late),
                         static_cast<unsigned long long>(stale), static_cast<unsigned long long>(underruns),
                         static_cast<unsigned long long>(g_mgr.counter),
                         static_cast<unsigned long long>(index_last), static_cast<unsigned long long>(index_writes),
                         fill);
            if (g_probe_key.load()) {
                /* The probe's own line: periods vs deliveries vs the counter the
                 * consumer advanced. All three from the same thread pair in the
                 * same interval, so "one event per period" is a ratio, not a claim. */
                uint64_t received = 0;
                if (auto q = lv2::queue_by_key(g_probe_key.load())) {
                    std::lock_guard ql(q->m);
                    received = q->received;
                }
                LOG_INFO("cellAudio", "N5b probe: period %llu, delivered %llu, received %llu, failed %llu",
                         static_cast<unsigned long long>(g_mgr.counter),
                         static_cast<unsigned long long>(g_probe_delivered.load()),
                         static_cast<unsigned long long>(received),
                         static_cast<unsigned long long>(g_probe_failed.load()));
            }
            late = stale = blocks = 0;
        }
        std::fill(stereo.begin(), stereo.end(), 0.0f);
        bool any = false;
        {
            std::lock_guard lk(g_lock);
            for (Port& p : g_mgr.ports) {
                if (p.state != PortState::started || !p.num_blocks) continue;
                if (p.addr && vm::range_mapped(p.addr, p.size)) {
                    mix_block(p, p.cur_pos, stereo.data());
                    any = true;
                    const uint8_t* src = vm::ptr(p.addr) + size_t(p.cur_pos) * p.num_channels * BLOCK_BYTES_PER_CHANNEL;
                    uint64_t h = 1469598103934665603ull;
                    bool silent = true;
                    for (uint32_t k = 0; k + 8 <= p.num_channels * BLOCK_BYTES_PER_CHANNEL; k += 8) {
                        uint64_t w;
                        std::memcpy(&w, src + k, 8);
                        silent = silent && !w;
                        h = (h ^ w) * 1099511628211ull;
                    }
                    if (!silent && p.cur_pos < 32 && block_hash[p.number][p.cur_pos] == h) stale++;
                    if (p.cur_pos < 32) block_hash[p.number][p.cur_pos] = h;
                    blocks++;
                }
            }
            /* One mixer period: advance cur_pos, publish every read index, and
             * send one CELL_AUDIO_EVENT_MIX per registered queue. This is the
             * same function the unit test drives. */
            audio::step(g_mgr, deliver);
        }
        if (any && ppu::time_scale() == 1.0) {
            for (float& v : stereo) v = std::clamp(v * 0.7f, -1.0f, 1.0f);
            out.write(stereo.data(), BLOCK_SAMPLES);
            static FILE* dump = std::getenv("GOWA_AUDIO_DUMP") ? std::fopen(std::getenv("GOWA_AUDIO_DUMP"), "wb") : nullptr;
            if (dump) std::fwrite(stereo.data(), 4, stereo.size(), dump);
        }
    }
}

}  // namespace

/* --- the calls into the host, audio_hle.h's interface --------------------- */

namespace audio {

uint32_t alloc_index_area(uint32_t bytes) {
    /* RPCS3 maps 0x40000-aligned shared memory of 0x10000 (cellAudio.cpp:1531),
     * and the index slots are 16 bytes each (:1604). */
    return vm::alloc(vm::Region::User64K, bytes, 0x10000);
}

void free_index_area(uint32_t addr) {
    if (addr) vm::dealloc(addr);
}

uint32_t alloc_port_buffer(uint32_t bytes) {
    const uint32_t size = mapped_size_for(bytes);
    return vm::alloc(vm::Region::User64K, size, 0x10000);
}

void free_port_buffer(uint32_t addr) {
    if (addr) vm::dealloc(addr);
}

void write_index(uint32_t index_addr, uint64_t value) {
    if (index_addr) vm::write64(index_addr, value);
}

uint64_t guest_time_us() {
    /* The guest clock, and the one cellAudioGetPortTimestamp reports
     * (RPCS3 get_guest_system_time, cellAudio.cpp:1964). */
    return lv2::system_time_us();
}

uint32_t queue_create(uint64_t key, uint32_t depth, const char* name) {
    return lv2::create_queue_by_key(key, depth, name);
}

bool queue_send(uint64_t key, uint64_t source, uint64_t d1, uint64_t d2, uint64_t d3) {
    return lv2::send_event_by_key(key, source, d1, d2, d3);
}

bool queue_exists(uint64_t key) {
    return lv2::queue_by_key(key) != nullptr;
}

uint32_t process_pid() {
    /* sys_process_getpid is 1 (runtime/src/lv2/process.cpp:20). */
    return 1;
}

uint32_t sdk_version() {
    return loader::g_process.sdk_version;
}

}  // namespace audio

/* --- the imports ---------------------------------------------------------- */

/* cellAudioInit() -- refs/.../cellAudio.cpp:1520-1616. */
HLE_FUNC(cellAudio, cellAudioInit) {
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::init(g_mgr);
    if (rc) {
        LOG_WARN("cellAudio", "cellAudioInit() -> 0x%08x", static_cast<uint32_t>(rc));
        return ret(ctx, static_cast<int64_t>(rc));
    }
    LOG_INFO("cellAudio", "cellAudioInit: index area 0x%08x (0x%x), %u ports, sdk 0x%x", g_mgr.index_area,
             g_mgr.index_area_size, g_mgr.free_count, loader::g_process.sdk_version);
    g_thread_live = true;
    std::thread(audio_thread).detach();
    if (std::getenv("PS3BASE_AUDIO_NOTIFY_PROBE")) {
        const uint64_t key = probe_register_notify_queue();
        if (!key) LOG_WARN("cellAudio", "N5b probe: could not create/register a notify queue");
        else {
            LOG_INFO("cellAudio", "N5b probe: notify queue key 0x%llx registered, receiver started",
                     static_cast<unsigned long long>(key));
            std::thread(probe_receiver, key).detach();
        }
    }
    ret(ctx, 0);
}

/* cellAudioQuit() -- refs/.../cellAudio.cpp:1617-1651. */
HLE_FUNC(cellAudio, cellAudioQuit) {
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::quit(g_mgr);
    g_thread_live = false;
    LOG_INFO("cellAudio", "cellAudioQuit() -> 0x%08x", static_cast<uint32_t>(rc));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioPortOpen(portParam, portNum) -- refs/.../cellAudio.cpp:1652-1776. */
HLE_FUNC(cellAudio, cellAudioPortOpen) {
    const uint32_t param = a32(ctx, 0), out = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    uint64_t attr = 0;
    float level = 0.0f;
    uint32_t size = 0;
    const int32_t rc = audio::port_open(g_mgr, param, out, &attr, &level, &size);
    if (rc) {
        LOG_WARN("cellAudio", "cellAudioPortOpen(param=0x%08x, portNum=*0x%08x) -> 0x%08x", param, out,
                 static_cast<uint32_t>(rc));
        return ret(ctx, static_cast<int64_t>(rc));
    }
    const uint32_t num = vm::read32(out);
    const Port& p = g_mgr.ports[num];
    LOG_INFO("cellAudio", "cellAudioPortOpen: port %u, %u ch, %u blocks, attr 0x%llx, level %.3f -> "
             "portSize 0x%x at 0x%08x, readIndexAddr 0x%08x (status 0x%x)",
             num, p.num_channels, p.num_blocks, static_cast<unsigned long long>(attr), static_cast<double>(level),
             p.size, p.addr, p.index, STATUS_READY);
    ret(ctx, 0);
}

/* cellAudioPortClose(portNum) -- refs/.../cellAudio.cpp:1863-1898. */
HLE_FUNC(cellAudio, cellAudioPortClose) {
    const uint32_t num = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::port_close(g_mgr, num);
    if (rc) LOG_WARN("cellAudio", "cellAudioPortClose(%u) -> 0x%08x", num, static_cast<uint32_t>(rc));
    else LOG_INFO("cellAudio", "cellAudioPortClose(%u) -> 0x0", num);
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioPortStop(portNum) -- refs/.../cellAudio.cpp:1900-1927. */
HLE_FUNC(cellAudio, cellAudioPortStop) {
    const uint32_t num = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::port_stop(g_mgr, num);
    if (rc) LOG_WARN("cellAudio", "cellAudioPortStop(%u) -> 0x%08x", num, static_cast<uint32_t>(rc));
    else LOG_INFO("cellAudio", "cellAudioPortStop(%u) -> 0x0", num);
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioGetPortConfig(portNum, portConfig) -- refs/.../cellAudio.cpp:1778-1821. */
HLE_FUNC(cellAudio, cellAudioGetPortConfig) {
    const uint32_t num = a32(ctx, 0), out = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::get_port_config(g_mgr, num, out);
    if (rc) {
        LOG_WARN("cellAudio", "cellAudioGetPortConfig(%u, 0x%08x) -> 0x%08x", num, out, static_cast<uint32_t>(rc));
        return ret(ctx, static_cast<int64_t>(rc));
    }
    const Port& p = g_mgr.ports[num];
    LOG_INFO("cellAudio", "cellAudioGetPortConfig(%u, 0x%08x): readIndexAddr 0x%08x status 0x%x nChannel %u nBlock %u "
             "portSize 0x%x portAddr 0x%08x",
             num, out, p.index, num < PORT_COUNT ? (p.state == PortState::started ? STATUS_RUN : p.state == PortState::opened ? STATUS_READY : STATUS_CLOSE) : 0,
             p.num_channels, p.num_blocks, p.size, p.addr);
    ret(ctx, 0);
}

/* cellAudioPortStart(portNum) -- refs/.../cellAudio.cpp:1823-1861. */
HLE_FUNC(cellAudio, cellAudioPortStart) {
    const uint32_t num = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::port_start(g_mgr, num);
    if (rc) LOG_WARN("cellAudio", "cellAudioPortStart(%u) -> 0x%08x", num, static_cast<uint32_t>(rc));
    else LOG_INFO("cellAudio", "cellAudioPortStart(%u) -> 0x0: buffer 0x%08x zeroed (%u bytes), readIndexAddr 0x%08x",
                  num, g_mgr.ports[num].addr, g_mgr.ports[num].size, g_mgr.ports[num].index);
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioGetPortTimestamp(portNum, tag, stamp) -- refs/.../cellAudio.cpp:1929-1968. */
HLE_FUNC(cellAudio, cellAudioGetPortTimestamp) {
    const uint32_t num = a32(ctx, 0), stamp = a32(ctx, 2);
    const uint64_t tag = a64(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::get_port_timestamp(g_mgr, num, tag, stamp);
    LOG_INFO("cellAudio", "cellAudioGetPortTimestamp(%u, tag 0x%llx) -> 0x%08x, stamp 0x%llx", num,
             static_cast<unsigned long long>(tag), static_cast<uint32_t>(rc),
             static_cast<unsigned long long>(stamp && vm::range_mapped(stamp, 8) ? vm::read64(stamp) : 0));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioGetPortBlockTag(portNum, blockNo, tag) -- refs/.../cellAudio.cpp:1970-2004. */
HLE_FUNC(cellAudio, cellAudioGetPortBlockTag) {
    const uint32_t num = a32(ctx, 0), tag_ea = a32(ctx, 2);
    const uint64_t block_no = a64(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::get_port_block_tag(g_mgr, num, block_no, tag_ea);
    LOG_INFO("cellAudio", "cellAudioGetPortBlockTag(%u, block 0x%llx) -> 0x%08x, tag 0x%llx", num,
             static_cast<unsigned long long>(block_no), static_cast<uint32_t>(rc),
             static_cast<unsigned long long>(tag_ea && vm::range_mapped(tag_ea, 8) ? vm::read64(tag_ea) : 0));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioCreateNotifyEventQueue(id, key) -- refs/.../cellAudio.cpp:2076-2083. */
HLE_FUNC(cellAudio, cellAudioCreateNotifyEventQueue) {
    const uint32_t id_ea = a32(ctx, 0), key_ea = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::create_notify_event_queue(g_mgr, id_ea, key_ea, 0);
    if (rc) LOG_WARN("cellAudio", "cellAudioCreateNotifyEventQueue(id=0x%08x, key=0x%08x) -> 0x%08x", id_ea, key_ea,
                     static_cast<uint32_t>(rc));
    else LOG_INFO("cellAudio", "cellAudioCreateNotifyEventQueue: id 0x%08x key 0x%llx", vm::read32(id_ea),
                  static_cast<unsigned long long>(vm::read64(key_ea)));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioCreateNotifyEventQueueEx(id, key, iFlags) -- refs/.../cellAudio.cpp:2083-2105. */
HLE_FUNC(cellAudio, cellAudioCreateNotifyEventQueueEx) {
    const uint32_t id_ea = a32(ctx, 0), key_ea = a32(ctx, 1), flags = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::create_notify_event_queue(g_mgr, id_ea, key_ea, flags);
    LOG_INFO("cellAudio", "cellAudioCreateNotifyEventQueueEx(id=0x%08x, key=0x%08x, iFlags=0x%x) -> 0x%08x", id_ea,
             key_ea, flags, static_cast<uint32_t>(rc));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioSetNotifyEventQueue(key) / Ex -- refs/.../cellAudio.cpp:2206-2249. */
HLE_FUNC(cellAudio, cellAudioSetNotifyEventQueue) {
    const uint64_t key = a64(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::set_notify_event_queue(g_mgr, key, 0);
    if (rc) LOG_WARN("cellAudio", "cellAudioSetNotifyEventQueue(0x%llx) -> 0x%08x",
                     static_cast<unsigned long long>(key), static_cast<uint32_t>(rc));
    else LOG_INFO("cellAudio", "cellAudioSetNotifyEventQueue(0x%llx) -> 0x0", static_cast<unsigned long long>(key));
    ret(ctx, static_cast<int64_t>(rc));
}

HLE_FUNC(cellAudio, cellAudioSetNotifyEventQueueEx) {
    const uint64_t key = a64(ctx, 0);
    const uint32_t flags = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::set_notify_event_queue(g_mgr, key, flags);
    LOG_INFO("cellAudio", "cellAudioSetNotifyEventQueueEx(0x%llx, 0x%x) -> 0x%08x",
             static_cast<unsigned long long>(key), flags, static_cast<uint32_t>(rc));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioRemoveNotifyEventQueue(key) / Ex -- refs/.../cellAudio.cpp:2258-2293. */
HLE_FUNC(cellAudio, cellAudioRemoveNotifyEventQueue) {
    const uint64_t key = a64(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::remove_notify_event_queue(g_mgr, key, 0);
    LOG_INFO("cellAudio", "cellAudioRemoveNotifyEventQueue(0x%llx) -> 0x%08x",
             static_cast<unsigned long long>(key), static_cast<uint32_t>(rc));
    ret(ctx, static_cast<int64_t>(rc));
}

HLE_FUNC(cellAudio, cellAudioRemoveNotifyEventQueueEx) {
    const uint64_t key = a64(ctx, 0);
    const uint32_t flags = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::remove_notify_event_queue(g_mgr, key, flags);
    LOG_INFO("cellAudio", "cellAudioRemoveNotifyEventQueueEx(0x%llx, 0x%x) -> 0x%08x",
             static_cast<unsigned long long>(key), flags, static_cast<uint32_t>(rc));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioSetPortLevel(portNum, level) -- refs/.../cellAudio.cpp:2006-2044. */
HLE_FUNC(cellAudio, cellAudioSetPortLevel) {
    const uint32_t num = a32(ctx, 0);
    float level = 0.0f;
    const uint32_t raw = static_cast<uint32_t>(a64(ctx, 1));
    std::memcpy(&level, &raw, 4);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::set_port_level(g_mgr, num, level);
    LOG_INFO("cellAudio", "cellAudioSetPortLevel(%u, %.3f) -> 0x%08x", num, static_cast<double>(level),
             static_cast<uint32_t>(rc));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioAddData(portNum, src, samples, volume) -- refs/.../cellAudio.cpp:2287-2318.
 * Only gta-sa imports this of the three titles here; before this it resolved
 * nowhere and the dispatcher answered a silent 0. */
HLE_FUNC(cellAudio, cellAudioAddData) {
    const uint32_t num = a32(ctx, 0), src = a32(ctx, 1), samples = a32(ctx, 2);
    float volume = 0.0f;
    const uint32_t raw = static_cast<uint32_t>(a64(ctx, 3));
    std::memcpy(&volume, &raw, 4);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::add_data(g_mgr, num, src, samples, volume);
    if (rc) LOG_WARN("cellAudio", "cellAudioAddData(%u, 0x%08x, %u, %.3f) -> 0x%08x", num, src, samples,
                     static_cast<double>(volume), static_cast<uint32_t>(rc));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioAdd2chData(portNum, src, samples, volume) -- refs/.../cellAudio.cpp:2320-2380. */
HLE_FUNC(cellAudio, cellAudioAdd2chData) {
    const uint32_t num = a32(ctx, 0), src = a32(ctx, 1), samples = a32(ctx, 2);
    float volume = 0.0f;
    const uint32_t raw = static_cast<uint32_t>(a64(ctx, 3));
    std::memcpy(&volume, &raw, 4);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::add_2ch_data(g_mgr, num, src, samples, volume);
    if (rc) LOG_WARN("cellAudio", "cellAudioAdd2chData(%u, 0x%08x, %u, %.3f) -> 0x%08x", num, src, samples,
                     static_cast<double>(volume), static_cast<uint32_t>(rc));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioAdd6chData(portNum, src, volume) -- refs/.../cellAudio.cpp:2382-2430. */
HLE_FUNC(cellAudio, cellAudioAdd6chData) {
    const uint32_t num = a32(ctx, 0), src = a32(ctx, 1);
    float volume = 0.0f;
    const uint32_t raw = static_cast<uint32_t>(a64(ctx, 2));
    std::memcpy(&volume, &raw, 4);
    std::lock_guard lk(g_lock);
    const int32_t rc = audio::add_6ch_data(g_mgr, num, src, volume);
    if (rc) LOG_WARN("cellAudio", "cellAudioAdd6chData(%u, 0x%08x, %.3f) -> 0x%08x", num, src,
                     static_cast<double>(volume), static_cast<uint32_t>(rc));
    ret(ctx, static_cast<int64_t>(rc));
}

/* cellAudioSendAck(data3) -- refs/.../cellAudio.cpp:2446-2472: the ack is
 * recorded against the source word and always answers OK once initialised. */
HLE_FUNC(cellAudio, cellAudioSendAck) {
    const uint64_t data3 = a64(ctx, 0);
    std::lock_guard lk(g_lock);
    if (!g_mgr.is_init) return ret(ctx, static_cast<int64_t>(ERR_NOT_INIT));
    for (KeyInfo& k : g_mgr.keys) {
        if (k.source == data3) {
            k.ack_timestamp = lv2::system_time_us();
            break;
        }
    }
    LOG_INFO("cellAudio", "cellAudioSendAck(0x%llx)", static_cast<unsigned long long>(data3));
    ret(ctx, 0);
}

/* cellAudioMiscSetAccessoryVolume(devNum, volume) -- refs/...:2432-2444. */
HLE_FUNC(cellAudio, cellAudioMiscSetAccessoryVolume) {
    std::lock_guard lk(g_lock);
    if (!g_mgr.is_init) return ret(ctx, static_cast<int64_t>(ERR_NOT_INIT));
    LOG_INFO("cellAudio", "cellAudioMiscSetAccessoryVolume(%u, %.3f)", a32(ctx, 0),
             static_cast<double>(*reinterpret_cast<const float*>(&ctx.gpr[4])));
    ret(ctx, 0);
}

/* cellAudioSetPersonalDevice / Unset -- refs/.../cellAudio.cpp:2474-2520. */
HLE_FUNC(cellAudio, cellAudioSetPersonalDevice) {
    const int32_t stream = s32(ctx, 0), device = s32(ctx, 1);
    std::lock_guard lk(g_lock);
    if (!g_mgr.is_init) return ret(ctx, static_cast<int64_t>(ERR_NOT_INIT));
    if (stream < 0 || stream >= 4 || (device != 0x8000 && (device < 0 || device >= 5))) {
        return ret(ctx, static_cast<int64_t>(ERR_PARAM));
    }
    LOG_INFO("cellAudio", "cellAudioSetPersonalDevice(%d, %d)", stream, device);
    ret(ctx, 0);
}

HLE_FUNC(cellAudio, cellAudioUnsetPersonalDevice) {
    const int32_t stream = s32(ctx, 0);
    std::lock_guard lk(g_lock);
    if (!g_mgr.is_init) return ret(ctx, static_cast<int64_t>(ERR_NOT_INIT));
    if (stream < 0 || stream >= 4) return ret(ctx, static_cast<int64_t>(ERR_PARAM));
    LOG_INFO("cellAudio", "cellAudioUnsetPersonalDevice(%d)", stream);
    ret(ctx, 0);
}

}
