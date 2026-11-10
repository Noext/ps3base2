#include "common.h"
#include "runtime/msgdialog.h"
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

bool g_init = false;
uint32_t g_max = 7;
uint32_t g_setting = 0;
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
            if (k > 300 && k < 360) b[3] |= 0x08;
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
    vm::write32(out, 24);
    for (int i = 0; i < 24; i++) vm::write16(out + 4 + i * 2, b[i]);
}

}

HLE_FUNC(sys_io, cellPadInit) {
    if (g_init) return ret(ctx, static_cast<int32_t>(0x80121103));
    g_init = true;
    g_max = a32(ctx, 0) > 7 ? 7 : a32(ctx, 0);
    g_start = ppu::timebase();
    ret(ctx, 0);
}

HLE_FUNC(sys_io, cellPadEnd) {
    g_init = false;
    ret(ctx, 0);
}

HLE_FUNC(sys_io, cellPadGetInfo2) {
    uint32_t out = a32(ctx, 0);
    if (!g_init) return ret(ctx, static_cast<int32_t>(0x80121104));
    std::memset(vm::ptr(out), 0, 4 * 3 + 4 * 7 * 4);
    vm::write32(out, g_max);
    vm::write32(out + 4, 1);
    vm::write32(out + 8, 0);
    vm::write32(out + 12, 1);
    vm::write32(out + 12 + 28, g_setting);
    vm::write32(out + 12 + 56, 0x7);
    vm::write32(out + 12 + 84, 0);
    ret(ctx, 0);
}

HLE_FUNC(sys_io, cellPadGetData) {
    uint32_t port = a32(ctx, 0), out = a32(ctx, 1);
    if (!g_init) return ret(ctx, static_cast<int32_t>(0x80121104));
    if (port >= g_max) return ret(ctx, static_cast<int32_t>(0x80121107));
    std::memset(vm::ptr(out), 0, 4 + 64 * 2);
    if (port != 0) return ret(ctx, static_cast<int32_t>(0x80121107));
    fill(out);
    ret(ctx, 0);
}

HLE_FUNC(sys_io, cellPadClearBuf) { ret(ctx, 0); }

HLE_FUNC(sys_io, cellPadSetPortSetting) {
    if (a32(ctx, 0) == 0) g_setting = a32(ctx, 1);
    ret(ctx, 0);
}

HLE_FUNC(sys_io, cellPadSetActDirect) { ret(ctx, 0); }

}
