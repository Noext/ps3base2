#pragma once

#include "spu/context.h"

#include <cstddef>
#include <cstdint>

namespace spu {

using BlockFn = bool (*)(Context&, Host&);

struct CompiledBlock {
    uint32_t lsa;
    uint32_t words;
    const uint32_t* code;
    BlockFn fn;
    bool pure;
    BlockFn region;
};

const CompiledBlock* compiled_blocks(size_t& count);

namespace aot {

constexpr uint32_t MAX_BLOCK_WORDS = 256;

bool active();
bool recording();
bool is_terminator(uint32_t word);
const CompiledBlock* find(const uint8_t* ls, uint32_t pc);
void record(const uint8_t* ls, uint32_t pc);
/* Log once per distinct block hash that the interpreter had to run, and abort
 * the run when GOWA_SPU_STRICT=1. Returns the running total. */
uint64_t note_interpreted(const uint8_t* ls, uint32_t pc);
void note_shadow_check(bool diverged);
uint64_t shadow_checks();
uint64_t shadow_diffs();
uint64_t interpreted_blocks();
uint64_t interpreted_insns_total();
void flush();

}

}
