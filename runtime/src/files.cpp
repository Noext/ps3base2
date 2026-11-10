#include "runtime/files.h"

#include <cstdio>
#include <mutex>

namespace files {

namespace {
std::mutex g_m;
Provider g_fn;
}

void set_provider(Provider fn) {
    std::lock_guard lk(g_m);
    g_fn = std::move(fn);
}

bool has_provider() {
    std::lock_guard lk(g_m);
    return static_cast<bool>(g_fn);
}

std::vector<uint8_t> read(const std::string& path) {
    {
        Provider fn;
        {
            std::lock_guard lk(g_m);
            fn = g_fn;
        }
        if (fn) {
            std::vector<uint8_t> out;
            if (fn(path, out) && !out.empty()) return out;
        }
    }
    std::vector<uint8_t> v;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return v;
    std::fseek(f, 0, SEEK_END);
    v.resize(static_cast<size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    if (std::fread(v.data(), 1, v.size(), f) != v.size()) v.clear();
    std::fclose(f);
    return v;
}

}  // namespace files
