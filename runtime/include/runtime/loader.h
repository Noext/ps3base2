#pragma once

#include <cstdint>
#include <string>

namespace loader {

struct ProcessInfo {
    uint32_t sdk_version = 0x430001;
    int32_t primary_prio = 1000;
    uint32_t primary_stacksize = 0x10000;
    uint32_t malloc_pagesize = 0x100000;
    uint32_t tls_vaddr = 0;
    uint32_t tls_filesz = 0;
    uint32_t tls_memsz = 0;
    uint32_t entry_opd = 0;
};

extern ProcessInfo g_process;
extern std::string g_root;

int find_image(const char* name);
int find_image_by_path(const std::string& path);
int find_image(const char* name);
bool load_image(int index);
bool image_loaded(int index);
std::string host_path(const std::string& relative);

/* PT_PROC_PARAM (Elf64_Phdr p_type 0x60000001, "LOOS+0x1"): the only place
 * the title states what priority its main thread runs at. The layout is not
 * guessed -- it is refs/rpcs3/rpcs3/Emu/Cell/PPUModule.cpp:2185-2192:
 *
 *     be_t<u32> size;
 *     be_t<u32> magic;            // 0x13bcc5f6, SYS_PROCESS_PARAM_MAGIC
 *     be_t<u32> version;
 *     be_t<u32> sdk_version;
 *     be_t<s32> primary_prio;
 *     be_t<u32> primary_stacksize;
 *     be_t<u32> malloc_pagesize;
 *     be_t<u32> ppc_seg;
 *
 * and the acceptance rule is RPCS3's (:2210-2213): the priority is taken only
 * when it is below 3072 and at least -512 for a debug/root process, else
 * 0..3071. ps3base is not a debug process, so the floor is 0; a rejected value
 * leaves the previous one in place, exactly as RPCS3 leaves primary_prio at
 * its default. Kept out of loader.cpp so a test can feed it the real bytes of
 * the three titles without linking the image table and the VFS. */
struct ProcParam {
    uint32_t size = 0;
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t sdk_version = 0;
    int32_t primary_prio = 0;
    uint32_t primary_stacksize = 0;
    uint32_t malloc_pagesize = 0;
    uint32_t ppc_seg = 0;
    bool magic_ok = false;
    bool prio_accepted = false;
    bool size_short = false;
};

/* Decode the section; returns false only when there are fewer than 4 bytes to
 * read, i.e. the program header is not even a size field. */
bool decode_proc_param(const uint8_t* p, size_t n, ProcParam& out);

/* Apply the decoded section to the process info, using RPCS3's acceptance
 * rules. Fields that fail their check are left untouched. */
void apply_proc_param(ProcParam& pp, ProcessInfo& info);

}
