/* ps3base -- RenderDoc capture trigger.

 * The API struct is taken from RenderDoc's own header. Nothing here guesses a
 * member offset or a field order: an earlier version of this file declared the
 * vtable by hand, called past the real TriggerCapture slot, and silently did
 * nothing while reporting a successful trigger.
 */
#include "runtime/log.h"

#include "renderdoc_app.h"

#include <cstdint>
#include <cstdlib>
#include <string>

#if defined(__unix__)
#include <dlfcn.h>
#endif

namespace rdcapture {

namespace {

RENDERDOC_API_1_6_0* g_api = nullptr;
bool g_load_attempted = false;
std::string g_last_path;

bool load()
{
    if (g_api || g_load_attempted) return g_api != nullptr;
    g_load_attempted = true;
#if defined(__unix__)
    /* RTLD_NOLOAD makes the failure message meaningful: it distinguishes "the
     * layer was never injected" from "the library exists but has no API". */
    void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    const bool already = mod != nullptr;
    if (!mod) mod = dlopen("librenderdoc.so", RTLD_NOW);
    if (!mod) {
        LOG_WARN("rd", "librenderdoc.so not loaded in this process (%s)", dlerror());
        return false;
    }
    LOG_INFO("rd", "librenderdoc.so %s", already ? "already present" : "loaded now");
    auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(mod, "RENDERDOC_GetAPI"));
    if (!get_api) {
        LOG_WARN("rd", "RENDERDOC_GetAPI not found: %s", dlerror());
        return false;
    }
    void* api = nullptr;
    const int rc = get_api(eRENDERDOC_API_Version_1_6_0, &api);
    if (rc != 1 || !api) {
        LOG_WARN("rd", "RENDERDOC_GetAPI(1.6.0) returned %d, api %p", rc, api);
        return false;
    }
    g_api = static_cast<RENDERDOC_API_1_6_0*>(api);
    if (!g_api->TriggerCapture) {
        LOG_WARN("rd", "TriggerCapture is null in the 1.6.0 API");
        g_api = nullptr;
        return false;
    }
    LOG_INFO("rd", "RenderDoc API 1.6.0 acquired");
    return true;
#else
    LOG_WARN("rd", "RenderDoc capture is only wired on unix in this build");
    return false;
#endif
}

}  // namespace

/* PS3BASE_RDCAPTURE=<flip>: set the capture template, trigger one capture at
 * that flip, and report the resulting path on the following flip. */
void maybe_capture(uint64_t flips)
{
    static const long target = std::getenv("PS3BASE_RDCAPTURE")
                                   ? std::atol(std::getenv("PS3BASE_RDCAPTURE"))
                                   : 0;
    if (!target) return;
    static int state = 0;
    if (state == 0) {
        if (static_cast<long>(flips) < target) return;
        if (!load()) {
            state = 3;   /* reported once, do not retry every flip */
            return;
        }
        if (const char* t = std::getenv("PS3BASE_RDCAPTURE_PATH")) {
            g_api->SetCaptureFilePathTemplate(t);
            LOG_INFO("rd", "capture template '%s'", t);
        }
        g_api->TriggerCapture();
        LOG_INFO("rd", "TriggerCapture() at flip %llu", static_cast<unsigned long long>(flips));
        state = 1;
        return;
    }
    if (state == 1) {
        /* The capture is written when the frame that triggered it ends, which is
         * after this flip returns. Wait a few frames before asking, or the count
         * is still zero. */
        static uint64_t trigger_flip = 0;
        if (!trigger_flip) trigger_flip = flips;
        if (flips < trigger_flip + 8) return;
        const uint32_t n = g_api->GetNumCaptures();
        LOG_INFO("rd", "GetNumCaptures() = %u", n);
        for (uint32_t i = 0; i < n; i++) {
            /* Signature from renderdoc_app.h: (idx, filename, pathlength,
             * timestamp). */
            uint32_t len = 0;
            uint64_t ts = 0;
            if (g_api->GetCapture(i, nullptr, &len, &ts) && len) {
                std::string path(len, '\0');
                if (g_api->GetCapture(i, path.data(), &len, &ts)) {
                    path.resize(len ? len - 1 : 0);
                    LOG_INFO("rd", "capture %u: '%s' (ts %llu)", i, path.c_str(),
                             static_cast<unsigned long long>(ts));
                    g_last_path = path;
                }
            }
        }
        state = 2;
    }
}

}  // namespace rdcapture
