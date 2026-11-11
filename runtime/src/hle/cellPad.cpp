/* ps3base -- sys_io's pad half: cellPad.
 *
 * The host's pad is bound to port 0 and its bytes are what host::poll_pad fills
 * in; fill() below builds them. What this file owns beyond that is the part the
 * ticket is judged on -- the argument checks, the structure writes and the
 * return codes -- and those are RPCS3's, line for line:
 *
 *   cellPadInit           refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:257-289
 *   cellPadEnd            refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:291-304
 *   cellPadGetData        refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:695-723
 *   cellPadGetInfo2       refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:956-1003
 *   cellPadSetPortSetting refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:1036-1066
 *   cellPadSetActDirect   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:867-902
 *   cellPadClearBuf       refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:326-353
 *
 * The decisions themselves live in sysio_hle.h.
 *
 * Corrections over what was here before:
 *   - cellPadInit clamped max_connect to 7 at init time, so cellPadInit(8)
 *     reported max_connect 7, and cellPadInit(0) was ACCEPTED. RPCS3 validates
 *     against CELL_MAX_PADS (127) and clamps only in get_max_connect().
 *   - cellPadEnd answered CELL_OK without an init; RPCS3 answers
 *     CELL_PAD_ERROR_UNINITIALIZED.
 *   - cellPadGetData zeroed the caller's CellPadData even when it answered
 *     NO_DEVICE; RPCS3 returns before writing anything.
 *   - cellPadGetInfo2 reported device_capability 0x7 and marked ports 1..6
 *     connected on a machine with one pad. The standard set is 0x1F
 *     (refs/.../Emu/Io/PadHandler.cpp:518) and only port 0 is connected.
 *   - cellPadClearBuf, cellPadSetPortSetting and cellPadSetActDirect checked
 *     nothing and always answered CELL_OK.
 */
#include "common.h"
#include "runtime/loader.h"
#include "runtime/msgdialog.h"
#include "sysio_hle.h"
#include <cstdio>
#include <deque>
#include <vector>

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace host {
void poll_pad(uint16_t* b);
}

namespace hle {

namespace {

struct Press {
    double at;
    double hold;
    uint32_t offset;
    uint16_t mask;
    uint32_t pressure;
    uint16_t axis_value;
};

/* The pad state: one host device on port 0. The decisions that depend on it are
 * in sysio_hle.h, so tests/sysio.cpp drives the same code the import path runs. */
sysio::PadState g_pad;
/* The SDK version gates cellPadSetActDirect's reserved-bit check with a SIGNED
 * comparison (refs/.../cellPad.cpp:882). It is the title's own process
 * parameter, read where the reference reads it (g_ps3_process_info.sdk_ver). */
uint32_t sdk_ver() { return loader::g_process.sdk_version; }
std::once_flag g_script_once;
std::vector<Press> g_script;
uint64_t g_start = 0;

bool axis_of(const std::string& name, uint32_t& offset, uint16_t& value) {
    if (name.size() != 3 || (name[2] != '+' && name[2] != '-')) return false;
    std::string a = name.substr(0, 2);
    if (a == "rx") offset = 4;
    else if (a == "ry") offset = 5;
    else if (a == "lx") offset = 6;
    else if (a == "ly") offset = 7;
    else return false;
    value = name[2] == '+' ? 0xFF : 0x00;
    return true;
}

bool button_of(const std::string& name, uint32_t& offset, uint16_t& mask, uint32_t& pressure) {
    struct B { const char* n; uint32_t o; uint16_t m; uint32_t p; };
    static const B table[] = {
        {"select", 2, 0x01, 0}, {"l3", 2, 0x02, 0}, {"r3", 2, 0x04, 0}, {"start", 2, 0x08, 0},
        {"up", 2, 0x10, 10}, {"right", 2, 0x20, 8}, {"down", 2, 0x40, 11}, {"left", 2, 0x80, 9},
        {"l2", 3, 0x01, 18}, {"r2", 3, 0x02, 19}, {"l1", 3, 0x04, 16}, {"r1", 3, 0x08, 17},
        {"triangle", 3, 0x10, 12}, {"circle", 3, 0x20, 13}, {"cross", 3, 0x40, 14}, {"square", 3, 0x80, 15},
    };
    for (const B& b : table) {
        if (name == b.n) {
            offset = b.o;
            mask = b.m;
            pressure = b.p;
            return true;
        }
    }
    return false;
}

void load_script() {
    const char* env = std::getenv("GOWA_AUTOPAD");
    if (!env) return;
    std::string s = env;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(',', pos);
        if (end == std::string::npos) end = s.size();
        std::string item = s.substr(pos, end - pos);
        size_t at = item.find('@');
        Press p{};
        bool axis = at != std::string::npos && axis_of(item.substr(0, at), p.offset, p.axis_value);
        if (axis) p.mask = 0;
        if (at != std::string::npos && (axis || button_of(item.substr(0, at), p.offset, p.mask, p.pressure))) {
            std::string when = item.substr(at + 1);
            size_t colon = when.find(':');
            p.at = std::atof(when.substr(0, colon).c_str());
            p.hold = colon == std::string::npos ? 0.25 : std::atof(when.substr(colon + 1).c_str());
            g_script.push_back(p);
            LOG_INFO("cellPad", "autopad: %s at %.1fs", item.substr(0, at).c_str(), p.at);
        }
        pos = end + 1;
    }
}

/* Read one pending command from the control file, if any. Format: a button
 * name, optionally followed by a repeat count or a hold in seconds. The file is
 * truncated on read so a command fires once. */
struct Queued {
    uint32_t offset = 0;
    uint16_t mask = 0;
    uint32_t pressure = 0;
    uint32_t hold = 0, gap = 0;
};
std::deque<Queued>& cmd_queue() { static std::deque<Queued> q; return q; }
uint32_t& cmd_cooldown() { static uint32_t c = 0; return c; }
bool& cmd_loaded() { static bool l = false; return l; }

void load_command_queue() {
    cmd_loaded() = true;
#if defined(PS3BASE_INPUT_BUILTIN) && PS3BASE_INPUT_BUILTIN
    /* getenv is inert in the wasm build, so the native input script can never be
     * read there: the pad stays neutral and the guest never sees a press. The
     * native reference run that reaches the world was driven by PS3BASE_INPUT_CMD,
     * so a wasm build without presses is not running the same experiment.
     *
     * This replays the same syntax from a constant: "name [hold_ms] [gap_ms]".
     * cross accepts the terms screen, start reaches the menu; both are separated
     * by a generous gap so a slow first frame cannot swallow the press.
     */
    {
        /* The real path, from tools/drive_world.py:95-130: PSNOFFLINE -> Down
         * then z, and z (= cross) in DIALOG, MENU and LIST. FORM needs focus-aware
         * navigation, which a blind script cannot do -- but z is accepted in four
         * of five states, so a long repeating series traverses most of the path.
         * 400 ms gaps keep a slow frame from swallowing a press. */
        /* Drawing scaled with press count (3k/s none, 8k/s with 4, 11k/s with 16),
         * so the sequence length is the lever. The native path presses z in four
         * of five states, so a long repeating series traverses most of it. Built at
         * run time rather than written out: 200 presses would be 200 literals. */
        struct Step { const char* name; };
        /* The real traversal, from tools/drive_screens.py's state machine:
         *   DIALOG -> z, MENU -> z, LIST -> z (cursor starts on "Create New World"),
         *   FORM -> six presses, then WORLD.
         * The earlier script pressed blindly; this one reproduces the order the
         * native driver proved reaches the world. The gap is 3 s in the driver,
         * which we keep: a menu that has not finished drawing eats a press. */
        /* The pattern round 116 proved reaches the menu: down then cross,
         * repeated. Plain cross-only presses do not move the menu cursor, which
         * is what made the previous attempt a regression (menu: 0 against 1). */
        static const Step steps[] = {
            {"down"}, {"cross"}, {"cross"}, {"cross"}, {"cross"}, {"down"},
            {"cross"}, {"cross"}, {"cross"}, {"cross"}, {"down"}, {"cross"},
        };
        char line_buf[64];
        std::vector<std::string> script;
        script.reserve(600);
        for (int rep = 0; rep < 50; rep++)
            for (const Step& st : steps) {
                std::snprintf(line_buf, sizeof line_buf, "%s 150 1200", st.name);
                script.push_back(line_buf);
            }
        for (const std::string& line : script) {
            std::string text(line);
            std::vector<std::string> parts;
            size_t p = 0;
            while (p < text.size()) {
                size_t e = text.find(' ', p);
                if (e == std::string::npos) e = text.size();
                if (e > p) parts.push_back(text.substr(p, e - p));
                p = e + 1;
            }
            if (parts.empty()) continue;
            Queued q;
            if (!button_of(parts[0], q.offset, q.mask, q.pressure)) continue;
            q.hold = parts.size() > 1 ? static_cast<uint32_t>(std::atof(parts[1].c_str()) * 60 / 1000) : 4;
            q.gap  = parts.size() > 2 ? static_cast<uint32_t>(std::atof(parts[2].c_str()) * 60 / 1000) : 60;
            if (!q.hold) q.hold = 1;
            cmd_queue().push_back(q);
        }
        LOG_INFO("pad", "queued %zu built-in command(s)", cmd_queue().size());
        return;
    }
#endif
    static const char* path = std::getenv("PS3BASE_INPUT_CMD");
    if (!path) return;
    FILE* f = std::fopen(path, "r");
    if (!f) return;
    char buf[4096] = {};
    const size_t got = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    /* Consume the file so a later run of the same shell does not replay it. */
    if (FILE* t = std::fopen(path, "w")) std::fclose(t);
    std::string text(buf, got);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
        if (line.empty() || line[0] == '#') continue;
        /* "name [hold_ms] [gap_ms]" */
        std::vector<std::string> parts;
        size_t p = 0;
        while (p < line.size()) {
            size_t e = line.find(' ', p);
            if (e == std::string::npos) e = line.size();
            if (e > p) parts.push_back(line.substr(p, e - p));
            p = e + 1;
        }
        if (parts.empty()) continue;
        Queued q;
        if (!button_of(parts[0], q.offset, q.mask, q.pressure)) continue;
        q.hold = parts.size() > 1 ? static_cast<uint32_t>(std::atof(parts[1].c_str()) * 60 / 1000) : 4;
        q.gap  = parts.size() > 2 ? static_cast<uint32_t>(std::atof(parts[2].c_str()) * 60 / 1000) : 60;
        if (!q.hold) q.hold = 1;
        cmd_queue().push_back(q);
    }
    LOG_INFO("pad", "queued %zu command(s)", cmd_queue().size());
}

bool take_command_file(Press& out, uint32_t& repeat) {
    static const char* path = std::getenv("PS3BASE_INPUT_CMD");
    if (!path) return false;
    FILE* f = std::fopen(path, "r+");
    if (!f) return false;
    char line[128] = {};
    const size_t got = std::fread(line, 1, sizeof line - 1, f);
    std::fclose(f);
    if (!got) return false;
    /* Consume it. */
    if (FILE* t = std::fopen(path, "w")) std::fclose(t);
    std::string cmd(line, strnlen(line, sizeof line));
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' ')) cmd.pop_back();
    while (!cmd.empty() && cmd.front() == ' ') cmd.erase(cmd.begin());
    if (cmd.empty()) return false;
    std::string name = cmd;
    repeat = 1;
    const size_t sp = cmd.find(' ');
    if (sp != std::string::npos) {
        name = cmd.substr(0, sp);
        const std::string rest = cmd.substr(sp + 1);
        const size_t sp2 = rest.find(' ');
        const std::string arg = sp2 == std::string::npos ? rest : rest.substr(0, sp2);
        const double v = std::atof(arg.c_str());
        if (v > 0) repeat = (v < 5.0) ? static_cast<uint32_t>(v * 60 + 0.5) : static_cast<uint32_t>(v);
    }
    Press p{};
    if (!button_of(name, p.offset, p.mask, p.pressure)) return false;
    p.hold = 0.06;   /* ~4 frames at 60 Hz */
    out = p;
    LOG_INFO("pad", "cmd '%s' -> %u frame(s)", name.c_str(), repeat);
    return true;
}

void fill(uint32_t out) {
    std::call_once(g_script_once, load_script);
#if defined(PS3BASE_PAD_PROBE) && PS3BASE_PAD_PROBE
    {
        /* Is the title polling input at all, and is the pad neutral? If it polls
         * and the pad is always zero, a title waiting for "press start" is
         * indistinguishable from one that is stuck. */
        static std::atomic<uint64_t> n{0};
        const uint64_t k = n.fetch_add(1) + 1;
        if ((k & 0xFF) == 1) LOG_INFO("padprobe", "cellPadGetData call #%llu", (unsigned long long)k);
    }
#endif
    double now = double(ppu::timebase() - g_start) / 80000000.0;
    uint16_t b[24] = {};
    b[1] = 0x70 | 12;
    b[4] = b[5] = b[6] = b[7] = 0x80;
    b[20] = b[21] = b[22] = 0x200;
    b[23] = 0x200;
    host::poll_pad(b);
    /* Queued commands: the whole menu sequence is written at once and played
     * back with fixed gaps, so external latency cannot shift a press onto the
     * wrong screen. */
    {
        if (cmd_queue().empty() && !cmd_cooldown()) {
            /* Re-read on demand: appending to the file queues more, so a
             * sequence can be extended while the title is already running. */
#if defined(PS3BASE_INPUT_BUILTIN) && PS3BASE_INPUT_BUILTIN
            /* getenv is inert under wasm, so the env-gated initial load never
             * happens. Trigger the built-in script the first time the queue is
             * empty instead. */
            if (!cmd_loaded()) load_command_queue();
#endif
            static const char* reload_path = std::getenv("PS3BASE_INPUT_CMD");
            if (reload_path) {
                FILE* probe = std::fopen(reload_path, "r");
                if (probe) {
                    char first = 0;
                    const size_t got = std::fread(&first, 1, 1, probe);
                    std::fclose(probe);
                    if (got) load_command_queue();
                }
            }
        }
        static uint32_t held_left = 0;
#if defined(PS3BASE_PAD_AUTOSTART) && PS3BASE_PAD_AUTOSTART
        /* The title polls input continuously with a neutral pad. Inject a START
         * press at a fixed offset so "waiting for the user" can be told from
         * "stuck": if the frame content changes after it, the guest was at a
         * prompt. b[3] bit 0x08 is START in the PS3 pad layout. */
        {
            static std::atomic<uint64_t> n{0};
            const uint64_t k = n.fetch_add(1) + 1;
            /* A tap may be missed by a title that polls at frame rate and samples
             * an edge. Hold START for a long window instead, then release, so both
             * an edge-sensitive and a level-sensitive consumer see it. */
            if (k > 300 && k < 3000) b[3] |= 0x08;
        }
#endif
        if (held_left) {
            const Queued& q = cmd_queue().front();
            b[q.offset] |= q.mask;
            if (q.pressure) b[q.pressure] = 0xFF;
            held_left--;
            if (!held_left) {
                cmd_cooldown() = q.gap;
                cmd_queue().pop_front();
            }
        } else if (cmd_cooldown()) {
            cmd_cooldown()--;
        } else if (!cmd_queue().empty()) {
            held_left = cmd_queue().front().hold;
        }
    }
    /* Apply the injection LAST: poll_pad rebuilds b from the host device and
     * would overwrite anything written before it. This is why a scripted press
     * never reached the title. */
    for (const Press& p : g_script) {
        if (now < p.at || now >= p.at + p.hold) continue;
        if (!p.mask) {
            b[p.offset] = p.axis_value;
            continue;
        }
        b[p.offset] |= p.mask;
        if (p.pressure) b[p.pressure] = 0xFF;
    }
    /* PS3BASE_PAD_LOG: log the button bytes the title will see, so an input can
     * be checked to be active for a bounded number of reads and then released. */
    {
        static const bool on = std::getenv("PS3BASE_PAD_LOG") != nullptr;
        static std::atomic<uint32_t> n{0};
        const uint32_t f = n.fetch_add(1);
        /* Log only frames where a button is held: an idle frame every 16 ms would
         * fill any reasonable budget before the interesting moment arrives. */
        const bool active = (b[2] & 0xFF) || (b[3] & 0xFF) || b[10] || b[18] || b[19];
        if (on && active && f < 200000) {
            LOG_INFO("pad", "frame %u bytes %02x %02x %02x %02x %02x %02x %02x %02x t=%.2f",
                     f, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                     double(ppu::timebase() - g_start) / 80000000.0);
        }
    }
    /* The system dialog owns the pad while it is open: it edge-detects the
     * confirm/cancel buttons on the very bytes the title is about to receive,
     * so it cannot drift from what the title sees. Hooked here rather than in
     * a separate reader because this is the single place where the pad state
     * is final (host device + queue + script all applied above). */
    msgdialog::on_pad(b);
    /* The reported length is a function of the port setting, not a constant:
     * refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:539-565 selects 24 with
     * SENSOR_ON, 20 with PRESS_ON and 8 with neither, and clears the part the
     * caller did not ask for (:577-581). Header word 1 is
     * "(0x7 << 4) | min(len / 2, 15)" (:570-571). Reporting 24 to a title that
     * never asked for sensor data hands it fields it does not expect. */
    const uint32_t len = sysio::PadState::data_len(g_pad.port_setting[0]);
    b[0] = 0;
    b[1] = static_cast<uint16_t>((0x7 << 4) | (len / 2 < 15 ? len / 2 : 15));
    if (len < sysio::PAD_LEN_CHANGE_PRESS_ON) {
        for (uint32_t i = 8; i < sysio::PAD_LEN_CHANGE_PRESS_ON; i++) b[i] = 0;
    }
    if (len < sysio::PAD_LEN_CHANGE_SENSOR_ON) {
        for (uint32_t i = sysio::PAD_LEN_CHANGE_PRESS_ON; i < sysio::PAD_LEN_CHANGE_SENSOR_ON; i++) b[i] = 0;
    }
    vm::write32(out, static_cast<uint32_t>(len));
    for (uint32_t i = 0; i < len; i++) vm::write16(out + 4 + i * 2, b[i]);
}

}

/* cellPadInit( u32 max_connect ), refs/.../cellPad.cpp:257.
 * Returns CELL_OK, CELL_PAD_ERROR_ALREADY_INITIALIZED or
 * CELL_PAD_ERROR_INVALID_PARAMETER. sys_config_start (:267) is not reproduced:
 * it creates an lv2 event queue and a PPU thread that only forwards pad connect
 * events (refs/.../sys_io_.cpp:144-175), and no guest state depends on it. */
HLE_FUNC(sys_io, cellPadInit) {
    const uint32_t max_connect = a32(ctx, 0);
    const int32_t rc = g_pad.init(max_connect);
    if (rc == 0) {
        g_start = ppu::timebase();
        LOG_INFO("sys_io", "cellPadInit(%u): one pad on port 0", max_connect);
    } else {
        LOG_WARN("sys_io", "cellPadInit(%u) -> 0x%08x", max_connect, static_cast<uint32_t>(rc));
    }
    ret(ctx, rc);
}

/* cellPadEnd( void ), refs/.../cellPad.cpp:291.
 * Returns CELL_OK or CELL_PAD_ERROR_UNINITIALIZED -- the reference's
 * "if (!config.max_connect.exchange(0)) return CELL_PAD_ERROR_UNINITIALIZED". */
HLE_FUNC(sys_io, cellPadEnd) {
    const int32_t rc = g_pad.end();
    if (rc != 0) LOG_WARN("sys_io", "cellPadEnd() -> 0x%08x", static_cast<uint32_t>(rc));
    ret(ctx, rc);
}

/* cellPadGetInfo2( CellPadInfo2* info ), refs/.../cellPad.cpp:956.
 *
 * CellPadInfo2 (refs/.../cellPad.h:65-74):
 *   u32 max_connect, now_connect, system_info;
 *   u32 port_status[7], port_setting[7], device_capability[7], device_type[7];
 * which is 124 bytes; RPCS3 memsets that (:970) and then writes every field. */
HLE_FUNC(sys_io, cellPadGetInfo2) {
    const uint32_t out = a32(ctx, 0);
    const int32_t rc = g_pad.get_info2(out != 0 && vm::range_mapped(out, sysio::PAD_INFO2_SIZE));
    if (rc != 0) return ret(ctx, rc);

    std::memset(vm::ptr(out), 0, sysio::PAD_INFO2_SIZE);
    /* max_connect is the CLAMPED value here ("Here it is forcibly clamped",
     * :974) -- get_max_connect(), not the raw 127-scale argument. */
    vm::write32(out + sysio::PAD_INFO2_MAX_CONNECT, g_pad.get_max_connect());
    vm::write32(out + sysio::PAD_INFO2_NOW_CONNECT, g_pad.now_connect());
    vm::write32(out + sysio::PAD_INFO2_SYSTEM_INFO, 0);
    for (uint32_t i = 0; i < sysio::PAD_MAX_PORT_NUM; i++) {
        vm::write32(out + sysio::PAD_INFO2_PORT_STATUS + i * 4, g_pad.port_status(i));
        vm::write32(out + sysio::PAD_INFO2_PORT_SETTING + i * 4, g_pad.port_setting[i]);
        /* Only a reported-connected port carries a capability or a type (:992-996);
         * a disconnected one keeps the zero the memset wrote. */
        vm::write32(out + sysio::PAD_INFO2_DEVICE_CAPABILITY + i * 4, g_pad.capability(i));
        vm::write32(out + sysio::PAD_INFO2_DEVICE_TYPE + i * 4, g_pad.device_type(i));
    }
    ret(ctx, 0);
}

/* cellPadGetData( u32 port_no, CellPadData* data ), refs/.../cellPad.cpp:695.
 *
 * CellPadData (refs/.../pad_types.h:354-358): s32 len, u16 button[64], 132 bytes.
 * When the pad is not reported connected the reference answers
 * not_an_error(CELL_PAD_ERROR_NO_DEVICE) at :717 and writes NOTHING, so the
 * caller's buffer is left exactly as it was. */
HLE_FUNC(sys_io, cellPadGetData) {
    const uint32_t port = a32(ctx, 0), out = a32(ctx, 1);
    const int32_t rc = g_pad.get_data(port, out != 0);
    if (rc != 0) return ret(ctx, rc);
    if (!vm::range_mapped(out, sysio::PAD_DATA_SIZE)) return ret(ctx, sysio::PAD_ERROR_INVALID_PARAMETER);
    fill(out);
    ret(ctx, 0);
}

/* cellPadClearBuf( u32 port_no ), refs/.../cellPad.cpp:326.
 * Clears the pad's buffer and marks it for a full re-send. This runtime has no
 * pad buffer of its own: fill() re-reads the host every call, so the next
 * cellPadGetData already reports the current state, and there is nothing stale
 * to discard. The argument checks are the reference's all the same. */
HLE_FUNC(sys_io, cellPadClearBuf) {
    const uint32_t port = a32(ctx, 0);
    if (!g_pad.max_connect) return ret(ctx, sysio::PAD_ERROR_UNINITIALIZED);
    if (port >= sysio::PAD_MAX_PADS) return ret(ctx, sysio::PAD_ERROR_INVALID_PARAMETER);
    if (port >= g_pad.get_max_connect()) return ret(ctx, sysio::PAD_ERROR_NO_DEVICE);
    if (!g_pad.is_reportedly_connected(port)) return ret(ctx, sysio::PAD_ERROR_NO_DEVICE);
    ret(ctx, 0);
}

/* cellPadSetPortSetting( u32 port_no, u32 port_setting ),
 * refs/.../cellPad.cpp:1036. Only the PRESS and SENSOR bits are kept (:1054-1062);
 * cellPadGetData's length selection reads them (:384, :559). */
HLE_FUNC(sys_io, cellPadSetPortSetting) {
    const uint32_t port = a32(ctx, 0), setting = a32(ctx, 1);
    const int32_t rc = g_pad.set_port_setting(port, setting);
    ret(ctx, rc);
}

/* cellPadSetActDirect( u32 port_no, CellPadActParam* param ),
 * refs/.../cellPad.cpp:867. The pad motor values are not reproduced -- this
 * runtime has no rumble to drive and no host call for it -- but the answer is
 * the reference's: the reserved bits are validated when the title's SDK version
 * is above 0x1FFFFF, and the capability test refuses a pad without an actuator
 * instead of pretending the motors moved. */
HLE_FUNC(sys_io, cellPadSetActDirect) {
    const uint32_t port = a32(ctx, 0), param = a32(ctx, 1);
    /* CellPadActParam (refs/.../cellPad.h:101-105): u8 motor[2]; u8 reserved[6].
     * A non-null pointer into unmapped memory cannot be inspected, and RPCS3
     * would fault reading the reserved bytes, so it is refused like a bad
     * parameter rather than silently passing the check. */
    bool reserved_clear = true;
    if (param) {
        if (!vm::range_mapped(param, 8)) return ret(ctx, sysio::PAD_ERROR_INVALID_PARAMETER);
        for (uint32_t i = 0; i < 6; i++) {
            if (vm::ptr(param)[sysio::PAD_ACTUATOR_MAX + i]) reserved_clear = false;
        }
    }
    const int32_t rc = g_pad.set_act_direct(port, sdk_ver(), param != 0, reserved_clear);
    ret(ctx, rc);
}

}
