#pragma once

#include "ppu/context.h"

namespace image {

struct FuncEntry {
    uint32_t addr;
    ppu::Func fn;
};

/* A landmark: an address a report must be able to name, generated at lift time
 * from a seed written as (image, offset).
 *
 * The seed FORM is what makes this stable. An absolute seed is resolved once and
 * goes stale on the next relayout; "liblv2+0xc7e8" is resolved by the lifter
 * against the base it actually gave liblv2, so the same landmark survives every
 * move of the module band.
 *
 * `entry` is the entry of the lifted function that CONTAINS this address, which
 * is the difference between "0x3000c7e8 (not lifted)" and
 * "liblv2 spurs-abort-tail+0x58". The byte is not a function entry: it is the
 * return address of the call at f_3000c790+0x54, and only the span says so.
 */
struct LandmarkEntry {
    const char* image;
    const char* label;
    /* The specification name the module exports for the function that contains
     * this address, or "" when it exports none. Kept beside the entry address
     * because the two answer different questions: the symbol is Sony's name for
     * the function, f_XXXX is the lifter's. */
    const char* symbol;
    uint32_t addr;
    uint32_t entry;
    uint32_t offset;
};

/* One lifted function's instruction range: [lo, hi] inclusive, and the image
 * that owns it.
 *
 * The dispatch table is keyed by entry, so "is there lifted code at this PC"
 * was false for every byte that was not a function's first instruction --
 * including the return address of every call. That is what produced
 * "lr=0x3000c7e8 (not lifted)" for a PC inside a lifted, running function.
 * The interval is what makes the question answerable.
 */
struct FunctionSpan {
    uint32_t lo;
    uint32_t hi;
    uint32_t entry;
    uint32_t image;
};

struct ImportEntry {
    const char* module;
    uint32_t nid;
    const char* name;
    uint32_t image;
};

struct ExportEntry {
    const char* module;
    uint32_t nid;
    const char* name;
    uint32_t image;
    uint32_t opd;
};

struct ImageEntry {
    const char* name;
    const char* file;
    bool prx;
    uint32_t text_lo;
    uint32_t text_hi;
    uint32_t toc;
    uint32_t entry;
};

/* Weak: the unit-test executables link runtime/src/memory.cpp (for the
 * allocator) without the generated table.cpp, and a strong symbol there would
 * fail the link. The real binaries always provide both. */
extern const ImageEntry images[] __attribute__((weak));
extern const uint32_t image_count __attribute__((weak));
extern const FuncEntry functions[];
extern const uint32_t function_count;

/* Weak for the same reason images[] is: a unit-test executable links the runtime
 * for one subsystem and provides no landmark table. */
extern const LandmarkEntry landmarks[] __attribute__((weak));
extern const uint32_t landmark_count __attribute__((weak));

extern const FunctionSpan function_spans[];
extern const uint32_t function_span_count;
extern const ImportEntry imports[];
extern const uint32_t import_count;
extern const ExportEntry exports[];
extern const uint32_t export_count;

}
