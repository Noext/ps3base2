#include "runtime/wgsl_emit.h"
#include "runtime/log.h"
#include "fp_decompiler.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace gpu {

namespace {

/* Bindings. Measured constraint (track H): WebGPU has no combined texture
 * sampler, so every RSX texture unit becomes two bindings. VP and FP each own 16
 * units; the four ranges do not overlap, so one bind group layout serves a
 * pipeline that runs both stages.
 *   VP textures 16..31   VP samplers 32..47
 *   FP textures 48..63   FP samplers 64..79
 * Uniform blocks keep the renderer's existing numbering (0 vertex constants,
 * 1 vertex state, 2 fragment constants, 3 fragment state). */
constexpr uint32_t B_VERTEX_CONSTANTS = 0;
constexpr uint32_t B_VERTEX_STATE = 1;
constexpr uint32_t B_FRAGMENT_CONSTANTS = 2;
constexpr uint32_t B_FRAGMENT_STATE = 3;
constexpr uint32_t B_VP_TEX = 16;
constexpr uint32_t B_VP_SMP = 32;
constexpr uint32_t B_FP_TEX = 48;
constexpr uint32_t B_FP_SMP = 64;

constexpr uint32_t VP_SLOTS = 544;
constexpr uint32_t FP_MAX_SLOTS = 4096;

const char* lane(uint32_t i) {
    static const char* l[4] = {"x", "y", "z", "w"};
    return l[i & 3];
}

const char* f2s(uint32_t v) {
    static thread_local char b[8][24];
    static thread_local int k = 0;
    k = (k + 1) & 7;
    std::snprintf(b[k], sizeof b[k], "%uu", v);
    return b[k];
}

/* GLSL bvecN does not exist in WGSL: a component-wise comparison yields
 * vecN<bool> and the numeric form of the same mask is a select(). Every RSX op
 * that the decompiler spells bvec4(...) goes through here. */
std::string mask_of(const std::string& cmp) { return "select(vec4<f32>(0.0), vec4<f32>(1.0), " + cmp + ")"; }

/* gl_PointSize has no WGSL builtin in this Chromium build (measured by track H:
 * getCompilationInfo lists 19 builtins and point_size is not among them). The RSX
 * point output therefore cannot be expressed here; the caller drops it and a
 * point primitive must be expanded to a quad by the vertex stage. Recorded, not
 * silently lost: see the note emitted into every vertex program that writes it. */

/* ------------------------------------------------------------------------- */
/* Vertex programs                                                           */
/* ------------------------------------------------------------------------- */

struct VInsn {
    uint32_t d0, d1, d2, d3;
    uint32_t vec_op() const { return (d1 >> 22) & 0x1F; }
    uint32_t sca_op() const { return (d1 >> 27) & 0x1F; }
    bool end() const { return d3 & 1; }
    uint32_t target() const { return (((d0 >> 23) & 1) << 9) | ((d2 & 0x3F) << 3) | ((d3 >> 29) & 7); }
};

bool vp_is_flow(uint32_t sca) { return sca == 0x08 || sca == 0x09 || sca == 0x0A || sca == 0x0B || sca == 0x0C || sca == 0x11 || sca == 0x12; }

bool vp_program_is_sane(const uint32_t* ucode, uint32_t start_slot, uint32_t slots) {
    if (!ucode || start_slot >= slots) return false;
    for (uint32_t s = start_slot; s < slots; s++) {
        if (ucode[s * 4 + 3] & 1u) return true;
    }
    return false;
}

class VertexEmitter {
public:
    VertexEmitter(const uint32_t* ucode, uint32_t start) : code_(ucode), start_(start) {}

    WgslShader run() {
        WgslShader out;
        if (!discover()) return out;
        std::ostringstream body;
        if (has_flow_) {
            body << "    var pc: i32 = " << start_ << ";\n";
            body << "    var stk: array<i32, 8>;\n    var sp: i32 = 0;\n    var guard: i32 = 0;\n";
            body << "    loop {\n        if (guard >= 512) { break; }\n        guard = guard + 1;\n        switch pc {\n";
            for (uint32_t s : slots_) {
                body << "        case " << s << ": {\n";
                emit(body, s, "            ");
                body << "        }\n";
            }
            body << "        default: { break; }\n        }\n        break;\n    }\n";
        } else {
            for (uint32_t s : order_) emit(body, s, "    ");
        }

        std::ostringstream src;
        src << "// RSX vertex program " << std::hex << std::uppercase << 0ull << std::dec;  /* hash written by caller */
        src.seekp(0);
        src.str("");
        src << "// RSX vertex program -> WGSL, emitted directly from the instruction stream.\n";
        src << "// No GLSL and no SPIR-V: glslang is not part of the wasm target.\n";
        src << "struct VertexConstants { c: array<vec4<f32>, 468> }\n";
        src << "struct VertexState { vp_scale: vec4<f32>, vp_offset: vec4<f32>, surface: vec4<f32>, flags: vec4<u32> }\n";
        src << "@group(0) @binding(" << B_VERTEX_CONSTANTS << ") var<uniform> vc: VertexConstants;\n";
        src << "@group(0) @binding(" << B_VERTEX_STATE << ") var<uniform> vstate: VertexState;\n";
        for (uint32_t t = 0; t < 4; t++) {
            if (!(textures_ & (1u << t))) continue;
            src << "@group(0) @binding(" << (B_VP_TEX + t) << ") var vtex" << t << ": texture_2d<f32>;\n";
            src << "@group(0) @binding(" << (B_VP_SMP + t) << ") var vsmp" << t << ": sampler;\n";
        }
        src << "struct VsIn {\n";
        for (uint32_t i = 0; i < 16; i++) {
            if (inputs_ & (1u << i)) src << "  @location(" << i << ") v" << i << ": vec4<f32>,\n";
        }
        if (!inputs_) src << "  @builtin(vertex_index) vzero: u32,\n";
        src << "}\n";
        src << "struct VsOut {\n  @builtin(position) position: vec4<f32>,\n";
        src << "  @location(1) out_col0: vec4<f32>,\n  @location(2) out_col1: vec4<f32>,\n  @location(3) out_fog: vec4<f32>,\n";
        for (uint32_t i = 0; i < 8; i++) src << "  @location(" << (4 + i) << ") out_tex" << i << ": vec4<f32>,\n";
        src << "  @location(12) out_tex8: vec4<f32>,\n  @location(13) out_tex9: vec4<f32>,\n";
        src << "  @location(14) out_bfc0: vec4<f32>,\n  @location(15) out_bfc1: vec4<f32>,\n}\n";
        /* The GLSL emits cread() with a mask and a range test; WGSL forbids an
         * out-of-range uniform index only in the sense that the value is
         * implementation-defined, so the index is clamped as well as tested. */
        src << "fn cread(i: i32) -> vec4<f32> {\n"
               "  let j = clamp(i, 0, 467);\n"
               "  return select(vec4<f32>(0.0), vc.c[j], i >= 0 && i < 468);\n"
               "}\n";
        src << "fn cctest(v: vec4<f32>, cond: i32) -> vec4<bool> {\n"
               "  var r: vec4<bool> = vec4<bool>(false, false, false, false);\n"
               "  if ((cond & 1) != 0) { r = r | (v < vec4<f32>(0.0)); }\n"
               "  if ((cond & 2) != 0) { r = r | (v == vec4<f32>(0.0)); }\n"
               "  if ((cond & 4) != 0) { r = r | (v > vec4<f32>(0.0)); }\n"
               "  return r;\n"
               "}\n";
        src << "@vertex\nfn vs_main(vi: VsIn) -> VsOut {\n";
        src << "  var R: array<vec4<f32>, 32>;\n";
        src << "  for (var i: i32 = 0; i < 32; i = i + 1) { R[i] = vec4<f32>(0.0); }\n";
        src << "  var o: array<vec4<f32>, 16>;\n";
        src << "  for (var i: i32 = 0; i < 16; i = i + 1) { o[i] = vec4<f32>(0.0, 0.0, 0.0, 1.0); }\n";
        src << "  var A: array<vec4<i32>, 2> = array<vec4<i32>, 2>(vec4<i32>(0), vec4<i32>(0));\n";
        src << "  var CC: array<vec4<f32>, 2> = array<vec4<f32>, 2>(vec4<f32>(0.0), vec4<f32>(0.0));\n";
        src << body.str();
        src << "  var omask: u32 = vstate.flags.x;\n";
        src << "  var out_col0: vec4<f32> = select(vec4<f32>(0.0, 0.0, 0.0, 1.0), o[1], (omask & 1u) != 0u);\n";
        src << "  var out_col1: vec4<f32> = select(vec4<f32>(0.0, 0.0, 0.0, 1.0), o[2], (omask & 2u) != 0u);\n";
        src << "  let two_side: bool = (vstate.flags.y & 1u) != 0u;\n";
        src << "  var out_bfc0: vec4<f32> = select(out_col0, o[3], two_side && (omask & 4u) != 0u);\n";
        src << "  var out_bfc1: vec4<f32> = select(out_col1, o[4], two_side && (omask & 8u) != 0u);\n";
        src << "  var out_fog: vec4<f32> = select(vec4<f32>(0.0), o[5].xxxx, (omask & 16u) != 0u);\n";
        for (uint32_t i = 0; i < 8; i++) {
            src << "  var out_tex" << i << ": vec4<f32> = select(vec4<f32>(0.0, 0.0, 0.0, 1.0), o[" << (7 + i) << "], (omask & " << (1u << (14 + i))
                << "u) != 0u);\n";
        }
        src << "  var out_tex8: vec4<f32> = select(vec4<f32>(0.0, 0.0, 0.0, 1.0), o[15], (omask & 4096u) != 0u);\n";
        src << "  var out_tex9: vec4<f32> = select(vec4<f32>(0.0, 0.0, 0.0, 1.0), o[6], (omask & 8192u) != 0u);\n";
        src << "  // gl_PointSize: WGSL has no point_size builtin (track H measured it absent from\n";
        src << "  // getCompilationInfo's builtin list), so the RSX point size is not emitted. A\n";
        src << "  // point primitive has to be expanded to a quad in this stage instead.\n";
        src << "  let p: vec4<f32> = o[0];\n";
        src << "  let xy: vec2<f32> = (vstate.vp_scale.xy * p.xy + (vstate.vp_offset.xy - vstate.surface.xy * 0.5) * p.w) * vstate.surface.zw * 2.0;\n";
        src << "  let position: vec4<f32> = vec4<f32>(xy, vstate.vp_scale.z * p.z + vstate.vp_offset.z * p.w, p.w);\n";
        src << "  return VsOut(position, out_col0, out_col1, out_fog";
        for (uint32_t i = 0; i < 8; i++) src << ", out_tex" << i;
        src << ", out_tex8, out_tex9, out_bfc0, out_bfc1);\n";
        src << "}\n";

        out.source = src.str();
        out.input_mask = inputs_;
        out.texture_mask = textures_;
        out.valid = true;
        return out;
    }

private:
    const uint32_t* code_;
    uint32_t start_;
    std::set<uint32_t> slots_;
    std::vector<uint32_t> order_;
    bool has_flow_ = false;
    uint32_t inputs_ = 0;
    uint32_t textures_ = 0;

    VInsn at(uint32_t s) const {
        if (!code_ || s >= VP_SLOTS) return {};
        return {code_[s * 4], code_[s * 4 + 1], code_[s * 4 + 2], code_[s * 4 + 3]};
    }

    bool discover() {
        std::vector<uint32_t> work{start_};
        while (!work.empty()) {
            uint32_t s = work.back();
            work.pop_back();
            while (s < VP_SLOTS && !slots_.count(s)) {
                slots_.insert(s);
                order_.push_back(s);
                VInsn i = at(s);
                if (vp_is_flow(i.sca_op())) {
                    has_flow_ = true;
                    if (i.sca_op() != 0x08 && i.sca_op() != 0x0B && i.sca_op() != 0x0C) work.push_back(i.target());
                }
                if (i.end()) break;
                s++;
            }
        }
        return !order_.empty();
    }

    std::string src(const VInsn& i, uint32_t n) {
        uint32_t raw = 0;
        if (n == 0) raw = ((i.d1 & 0xFF) << 9) | (i.d2 >> 23);
        else if (n == 1) raw = (i.d2 >> 6) & 0x1FFFF;
        else raw = ((i.d2 & 0x3F) << 11) | (i.d3 >> 21);
        uint32_t type = raw & 3;
        std::string base;
        if (type == 1) {
            base = "R[" + std::to_string((raw >> 2) & 0x3F) + "]";
        } else if (type == 2) {
            /* Reading an input slot is what makes it an input: the mask is the
             * set of attributes this program read, so the VsIn struct and this
             * reference always agree -- there is no declaration to disagree with. */
            uint32_t in = (i.d1 >> 8) & 0xF;
            inputs_ |= 1u << in;
            base = "vi.v" + std::to_string(in);
        } else if (type == 3) {
            uint32_t ci = (i.d1 >> 12) & 0x3FF;
            if (i.d3 & 2) {
                uint32_t a = (i.d0 >> 24) & 1;
                base = "cread((" + std::to_string(ci) + " + A[" + std::to_string(a) + "]." + lane(i.d0 & 3) + ") & 511)";
            } else {
                base = "cread(" + std::to_string(ci) + ")";
            }
        } else {
            base = "vec4<f32>(0.0)";
        }
        std::string sw = std::string(".") + lane(raw >> 14) + lane(raw >> 12) + lane(raw >> 10) + lane(raw >> 8);
        std::string v = "(" + base + ")" + sw;
        bool absf = n == 0 ? (i.d0 >> 21) & 1 : n == 1 ? (i.d0 >> 22) & 1 : (i.d0 >> 23) & 1;
        if (n == 2 && vp_is_flow(i.sca_op())) absf = false;
        if (absf) v = "abs(" + v + ")";
        if ((raw >> 16) & 1) v = "(-" + v + ")";
        return v;
    }

    std::string vec_expr(const VInsn& i, uint32_t op) {
        switch (op) {
        case 0x01: return src(i, 0);
        case 0x02: return src(i, 0) + " * " + src(i, 1);
        case 0x03: return src(i, 0) + " + " + src(i, 2);
        case 0x04: return "fma(" + src(i, 0) + ", " + src(i, 1) + ", " + src(i, 2) + ")";
        case 0x05: return "vec4<f32>(dot(" + src(i, 0) + ".xyz, " + src(i, 1) + ".xyz))";
        case 0x06: return "vec4<f32>(dot(" + src(i, 0) + ".xyz, " + src(i, 1) + ".xyz) + " + src(i, 1) + ".w)";
        case 0x07: return "vec4<f32>(dot(" + src(i, 0) + ", " + src(i, 1) + "))";
        /* GLSL vec4(lessThan(a,b)) converts a bvec4 to 1.0/0.0 lanes. */
        case 0x0B: return mask_of(src(i, 0) + " < " + src(i, 1));
        case 0x0C: return mask_of(src(i, 0) + " >= " + src(i, 1));
        case 0x10: return mask_of(src(i, 0) + " == " + src(i, 1));
        case 0x12: return mask_of(src(i, 0) + " > " + src(i, 1));
        case 0x13: return mask_of(src(i, 0) + " <= " + src(i, 1));
        case 0x14: return mask_of(src(i, 0) + " != " + src(i, 1));
        case 0x08: return "vec4<f32>(1.0, " + src(i, 0) + ".y * " + src(i, 1) + ".y, " + src(i, 0) + ".z, " + src(i, 1) + ".w)";
        case 0x09: return "min(" + src(i, 0) + ", " + src(i, 1) + ")";
        case 0x0A: return "max(" + src(i, 0) + ", " + src(i, 1) + ")";
        case 0x0E: return "fract(" + src(i, 0) + ")";
        case 0x0F: return "floor(" + src(i, 0) + ")";
        case 0x11: return "vec4<f32>(0.0)";
        case 0x15: return "vec4<f32>(1.0)";
        case 0x16: return "sign(" + src(i, 0) + ")";
        case 0x19: {
            uint32_t t = (i.d2 >> 8) & 3;
            textures_ |= 1u << t;
            return "textureSampleLevel(vtex" + std::to_string(t) + ", vsmp" + std::to_string(t) + ", (" + src(i, 0) + ").xy, 0.0)";
        }
        default: return "";
        }
    }

    std::string sca_expr(const VInsn& i, uint32_t op) {
        std::string x = src(i, 2) + ".x";
        switch (op) {
        case 0x01: return "vec4<f32>(" + x + ")";
        case 0x02: return "vec4<f32>(1.0 / " + x + ")";
        case 0x03: return "vec4<f32>(sign(" + x + ") * clamp(1.0 / abs(" + x + "), 5.42101e-20, 1.884467e19))";
        case 0x04: return "vec4<f32>(inverseSqrt(abs(" + x + ")))";
        case 0x05: return "vec4<f32>(exp2(floor(" + x + ")), fract(" + x + "), exp2(" + x + "), 1.0)";
        case 0x06:
            return "vec4<f32>(floor(log2(abs(" + x + "))), abs(" + x + ") / exp2(floor(log2(abs(" + x + ")))), log2(abs(" + x + ")), 1.0)";
        case 0x07: {
            std::string t = src(i, 2);
            return "vec4<f32>(1.0, max(" + t + ".x, 0.0), select(0.0, pow(max(" + t + ".y, 1e-10), clamp(" + t + ".w, -128.0, 128.0)), " + t +
                   ".x > 0.0), 1.0)";
        }
        case 0x0D: return "vec4<f32>(log2(" + x + "))";
        case 0x0E: return "vec4<f32>(exp2(" + x + "))";
        case 0x0F: return "vec4<f32>(sin(" + x + "))";
        case 0x10: return "vec4<f32>(cos(" + x + "))";
        default: return "";
        }
    }

    std::string cond_mask(const VInsn& i) {
        uint32_t cond = (i.d0 >> 10) & 7;
        uint32_t sel = (i.d0 >> 25) & 1;
        std::string sw = std::string(".") + lane(i.d0 >> 8) + lane(i.d0 >> 6) + lane(i.d0 >> 4) + lane(i.d0 >> 2);
        return "cctest(CC[" + std::to_string(sel) + "]" + sw + ", " + std::to_string(cond) + ")";
    }

    void masked_write(std::ostringstream& o, const std::string& ind, const std::string& dst, const std::string& val, uint32_t mask, bool cond) {
        for (uint32_t l = 0; l < 4; l++) {
            if (!(mask & (8u >> l))) continue;
            if (cond) {
                o << ind << "if (ct." << lane(l) << ") { " << dst << "." << lane(l) << " = " << val << "." << lane(l) << "; }\n";
            } else {
                o << ind << dst << "." << lane(l) << " = " << val << "." << lane(l) << ";\n";
            }
        }
    }

    void emit(std::ostringstream& o, uint32_t s, const std::string& ind) {
        VInsn i = at(s);
        uint32_t vop = i.vec_op(), sop = i.sca_op();
        bool cond = (i.d0 >> 13) & 1;
        uint32_t condv = (i.d0 >> 10) & 7;
        bool sat = (i.d0 >> 26) & 1;
        uint32_t cc_sel = (i.d0 >> 25) & 1;
        bool vec_result = (i.d0 >> 30) & 1;
        uint32_t dst = (i.d3 >> 2) & 0x1F;
        uint32_t vtmp = (i.d0 >> 15) & 0x3F;
        uint32_t stmp = (i.d3 >> 7) & 0x3F;
        uint32_t vmask = (i.d3 >> 13) & 0xF;
        uint32_t smask = (i.d3 >> 17) & 0xF;
        bool upd_v = (i.d0 >> 14) & 1;
        bool upd_s = (i.d0 >> 29) & 1;
        o << ind << "{\n";
        std::string in = ind + "    ";
        if (cond && condv == 0) {
            o << ind << "}\n";
            if (has_flow_) {
                if (i.end()) o << ind << "pc = -1;\n                break;\n";
                else if (!slots_.count(s + 1)) o << ind << "pc = " << (s + 1) << ";\n                continue;\n";
            }
            return;
        }
        if (cond) o << in << "let ct: vec4<bool> = " << cond_mask(i) << ";\n";
        std::string vexpr = vop == 0x0D ? "" : vec_expr(i, vop);
        std::string sexpr = vp_is_flow(sop) ? "" : sca_expr(i, sop);
        if (vop == 0x0D) o << in << "let ar: vec4<i32> = vec4<i32>(floor(" << src(i, 0) << "));\n";
        else if (!vexpr.empty()) o << in << "let vr: vec4<f32> = " << (sat ? "clamp(" + vexpr + ", vec4<f32>(0.0), vec4<f32>(1.0))" : vexpr) << ";\n";
        if (!sexpr.empty()) o << in << "let sr: vec4<f32> = " << (sat ? "clamp(" + sexpr + ", vec4<f32>(0.0), vec4<f32>(1.0))" : sexpr) << ";\n";
        if (vop == 0x0D) {
            std::string a = "A[" + std::to_string(vtmp & 1) + "]";
            for (uint32_t l = 0; l < 4; l++) {
                if (!(vmask & (8u >> l))) continue;
                if (cond) o << in << "if (ct." << lane(l) << ") { " << a << "." << lane(l) << " = ar." << lane(l) << "; }\n";
                else o << in << a << "." << lane(l) << " = ar." << lane(l) << ";\n";
            }
        } else if (!vexpr.empty()) {
            bool wrote = false;
            if (vtmp != 0x3F) {
                masked_write(o, in, "R[" + std::to_string(vtmp) + "]", "vr", vmask, cond);
                wrote = true;
            }
            if (vec_result && dst < 16) {
                masked_write(o, in, "o[" + std::to_string(dst) + "]", "vr", vmask, cond);
                wrote = true;
            }
            if (!wrote || upd_v) masked_write(o, in, "CC[" + std::to_string(cc_sel) + "]", "vr", vmask, cond);
        }
        if (!sexpr.empty()) {
            bool wrote = false;
            if (stmp != 0x3F) {
                masked_write(o, in, "R[" + std::to_string(stmp) + "]", "sr", smask, cond);
                wrote = true;
            } else if (!vec_result && dst < 16) {
                masked_write(o, in, "o[" + std::to_string(dst) + "]", "sr", smask, cond);
                wrote = true;
            }
            if (!wrote || upd_s) masked_write(o, in, "CC[" + std::to_string(cc_sel) + "]", "sr", smask, cond);
        }
        if (has_flow_ && vp_is_flow(sop)) {
            std::string take = condv == 7 ? "true" : condv == 0 ? "false" : "any(" + cond_mask(i) + ")";
            uint32_t t = i.target();
            switch (sop) {
            case 0x09: o << in << "if (" << take << ") { pc = " << t << "; continue; }\n"; break;
            case 0x0A: o << in << "if (" << take << " && sp < 8) { stk[sp] = " << (s + 1) << "; sp = sp + 1; pc = " << t << "; continue; }\n"; break;
            case 0x0C:
                o << in << "if (" << take << ") { if (sp == 0) { pc = -1; break; } sp = sp - 1; pc = stk[sp]; continue; }\n";
                break;
            case 0x11:
            case 0x12: {
                uint32_t bit = (i.d3 >> 23) & 0x1F;
                uint32_t want = (i.d3 >> 28) & 1;
                std::string test = "((vstate.flags.w >> " + std::to_string(bit) + "u) & 1u) == " + std::to_string(want) + "u";
                if (sop == 0x11) o << in << "if (" << test << ") { pc = " << t << "; continue; }\n";
                else o << in << "if (" << test << " && sp < 8) { stk[sp] = " << (s + 1) << "; sp = sp + 1; pc = " << t << "; continue; }\n";
                break;
            }
            default: break;
            }
        }
        o << ind << "}\n";
        if (has_flow_) {
            if (i.end()) o << ind << "pc = -1;\n" << ind << "break;\n";
            else if (!slots_.count(s + 1)) o << ind << "pc = " << (s + 1) << ";\n" << ind << "continue;\n";
        }
    }
};

/* ------------------------------------------------------------------------- */
/* Fragment programs                                                         */
/* ------------------------------------------------------------------------- */

struct FpSlot {
    uint32_t w[4];
    uint32_t opcode() const { return ((w[0] >> 24) & 0x3F) | (((w[2] >> 31) & 1) << 6); }
    bool end() const { return w[0] & 1; }
    bool uses_literal() const {
        for (int i = 1; i < 4; i++) {
            if ((w[i] & 3) == 2) return true;
        }
        return false;
    }
};

bool fp_is_flow(uint32_t op) { return op >= 0x40 && op <= 0x45; }

FpSlot fp_read_slot(const FpWordReader& read, uint32_t s) {
    return {{read(s * 4), read(s * 4 + 1), read(s * 4 + 2), read(s * 4 + 3)}};
}

/* exact Chinese-remainder style wrapping of a GLSL ternary into WGSL select();
 * select takes (false, true, cond). */
std::string sel(const std::string& when_false, const std::string& when_true, const std::string& cond) {
    return "select(" + when_false + ", " + when_true + ", " + cond + ")";
}

class FragmentEmitter {
public:
    FragmentEmitter(const FpWordReader& read, const FragmentProgramKey& key, const FragmentProgramInfo& info)
        : read_(read), key_(key), info_(info) {}

    WgslShader run() {
        WgslShader out;
        scan_registers();
        std::map<uint32_t, uint32_t> literal_index;
        for (uint32_t i = 0; i < info_.literal_slots.size(); i++) literal_index[info_.literal_slots[i]] = i;
        std::ostringstream body;
        std::vector<std::pair<uint32_t, std::string>> closers;
        uint32_t p = 0;
        std::string ind = "  ";
        while (p < info_.length_slots) {
            for (size_t c = closers.size(); c-- > 0;) {
                if (closers[c].first == p) {
                    ind.resize(ind.size() - 2);
                    body << ind << closers[c].second << "\n";
                    if (closers[c].second != "}") ind += "  ";
                    closers.erase(closers.begin() + c);
                }
            }
            FpSlot sl = fp_read_slot(read_, info_.start_slot + p);
            uint32_t op = sl.opcode();
            bool lit = sl.uses_literal() && (!fp_is_flow(op) || op == 0);
            current_literal_ = lit ? literal_index[p + 1] : 0;
            if (fp_is_flow(op)) emit_flow(body, sl, op, p, ind, closers);
            else if (op != 0) emit_op(body, sl, op, ind);
            p += lit ? 2 : 1;
            if (sl.end()) break;
        }
        while (!closers.empty()) {
            ind.resize(ind.size() > 2 ? ind.size() - 2 : 2);
            body << ind << "}\n";
            closers.pop_back();
        }

        std::ostringstream s;
        s << "// RSX fragment program -> WGSL, emitted directly from the instruction stream.\n";
        s << "// No GLSL and no SPIR-V. Texture and sampler are two bindings (WebGPU has no\n";
        s << "// combined sampler object), which is the structural change track H measured.\n";
        s << "struct FragmentConstants { fc: array<vec4<f32>, " << (info_.literal_slots.empty() ? 1u : static_cast<uint32_t>(info_.literal_slots.size())) << "> }\n";
        s << "struct FragmentState { window: vec4<f32>, fog: vec4<f32>, fflags: vec4<u32>, texscale: array<vec4<f32>, 4> }\n";
        s << "@group(0) @binding(" << B_FRAGMENT_CONSTANTS << ") var<uniform> fcvals: FragmentConstants;\n";
        s << "@group(0) @binding(" << B_FRAGMENT_STATE << ") var<uniform> fstate: FragmentState;\n";
        for (uint32_t u = 0; u < 16; u++) {
            if (!(used_tex_ & (1u << u))) continue;
            bool shadow = ((key_.shadow >> u) & 1) && dim(u) == 1;
            if (shadow) {
                s << "@group(0) @binding(" << (B_FP_TEX + u) << ") var tex" << u << ": texture_depth_2d;\n";
                s << "@group(0) @binding(" << (B_FP_SMP + u) << ") var smp" << u << ": sampler_comparison;\n";
            } else {
                const char* ty = dim(u) == 0 ? "texture_1d<f32>" : dim(u) == 2 ? "texture_cube<f32>" : dim(u) == 3 ? "texture_3d<f32>" : "texture_2d<f32>";
                s << "@group(0) @binding(" << (B_FP_TEX + u) << ") var tex" << u << ": " << ty << ";\n";
                s << "@group(0) @binding(" << (B_FP_SMP + u) << ") var smp" << u << ": sampler;\n";
            }
        }
        s << "struct FsIn {\n  @builtin(position) position: vec4<f32>,\n  @builtin(front_facing) front_facing: bool,\n";
        for (uint32_t l = 1; l <= 15; l++) {
            if (l == 1) s << "  @location(1) in_col0: vec4<f32>,\n";
            else if (l == 2) s << "  @location(2) in_col1: vec4<f32>,\n";
            else if (l == 3) s << "  @location(3) in_fog: vec4<f32>,\n";
            else if (l <= 11) s << "  @location(" << l << ") in_tex" << (l - 4) << ": vec4<f32>,\n";
            else if (l == 12) s << "  @location(12) in_tex8: vec4<f32>,\n";
            else if (l == 13) s << "  @location(13) in_tex9: vec4<f32>,\n";
            else if (l == 14) s << "  @location(14) in_bfc0: vec4<f32>,\n";
            else s << "  @location(15) in_bfc1: vec4<f32>,\n";
        }
        s << "}\n";
        uint32_t mrt = key_.mrt_count > 4 ? 4 : (key_.mrt_count ? key_.mrt_count : 1);
        bool depth = (key_.control & 0xE) != 0;
        s << "struct FsOut {\n";
        if (depth) s << "  @builtin(frag_depth) frag_depth: f32,\n";
        for (uint32_t m = 0; m < mrt; m++) s << "  @location(" << m << ") color" << m << ": vec4<f32>,\n";
        s << "}\n";

        /* Module-scope state the GLSL keeps as file-scope globals. WGSL zero-fills
         * module-scope variables, and main() resets CC explicitly. */
        s << "var<private> g_frag_coord: vec4<f32>;\n";
        s << "var<private> g_front_facing: bool;\n";
        for (const std::string& r : regs_) s << "var<private> " << r << ": vec4<f32>;\n";
        s << "var<private> CC: array<vec4<f32>, 2>;\n";
        s << "fn hget(r: vec4<f32>, hi: i32) -> vec4<f32> {\n"
             "  let a: vec2<f32> = select(r.xy, r.zw, hi == 1);\n"
             "  let lo: vec2<f32> = unpack2x16float(bitcast<u32>(a.x));\n"
             "  let hi2: vec2<f32> = unpack2x16float(bitcast<u32>(a.y));\n"
             "  return vec4<f32>(lo, hi2);\n"
             "}\n";
        s << "fn hset(r: vec4<f32>, hi: i32, h: vec4<f32>) -> vec4<f32> {\n"
             "  let p: vec2<f32> = vec2<f32>(bitcast<f32>(pack2x16float(h.xy)), bitcast<f32>(pack2x16float(h.zw)));\n"
             "  var out: vec4<f32> = r;\n"
             "  if (hi == 0) { out = vec4<f32>(p, out.zw); } else { out = vec4<f32>(out.xy, p); }\n"
             "  return out;\n"
             "}\n";
        s << "fn sat(v: vec4<f32>) -> vec4<f32> {\n"
             "  return clamp(select(v, vec4<f32>(0.0), v != v), vec4<f32>(0.0), vec4<f32>(1.0));\n"
             "}\n";
        s << "fn cctest(v: vec4<f32>, b: u32) -> vec4<bool> {\n"
             "  var r: vec4<bool> = vec4<bool>(false, false, false, false);\n"
             "  if ((b & 1u) != 0u) { r = r | (v < vec4<f32>(0.0)); }\n"
             "  if ((b & 2u) != 0u) { r = r | (v == vec4<f32>(0.0)); }\n"
             "  if ((b & 4u) != 0u) { r = r | (v > vec4<f32>(0.0)); }\n"
             "  return r;\n"
             "}\n";
        s << "fn wpos(inp: FsIn) -> vec4<f32> {\n"
             "  var f: vec4<f32> = g_frag_coord;\n"
             "  f = vec4<f32>(f.xy / max(fstate.fog.z, 1.0), f.zw);\n"
             "  let y: f32 = select(f.y, fstate.window.x - f.y, fstate.window.y > 0.5);\n"
             "  let p: vec2<f32> = vec2<f32>(f.x, y) - select(vec2<f32>(0.0), vec2<f32>(0.5), fstate.window.z > 0.5);\n"
             "  return vec4<f32>(p, f.z, f.w);\n"
             "}\n";
        s << "fn fogc(inp: FsIn) -> vec4<f32> {\n"
             "  let c: f32 = inp.in_fog.x;\n"
             "  let mode: u32 = fstate.fflags.y;\n"
             "  let a: f32 = select(c, abs(c), mode >= 0x802u);\n"
             "  var y: f32;\n"
             "  if (mode == 0x2601u || mode == 0x804u) { y = fstate.fog.y * a + (fstate.fog.x - 1.0); }\n"
             "  else if (mode == 0x800u || mode == 0x802u) { y = exp(11.084 * (fstate.fog.y * a + fstate.fog.x - 1.5)); }\n"
             "  else { let k: f32 = 4.709 * (fstate.fog.y * a + fstate.fog.x - 1.5); y = exp(-(k * k)); }\n"
             "  return vec4<f32>(c, clamp(y, 0.0, 1.0), 0.0, 0.0);\n"
             "}\n";
        s << "fn lit(a: vec4<f32>) -> vec4<f32> {\n"
             "  let x: f32 = max(a.x, 0.0);\n"
             "  return vec4<f32>(1.0, x, select(0.0, pow(max(a.y, 1e-10), a.w), x > 0.0), 1.0);\n"
             "}\n";
        s << "fn nrm(a: vec4<f32>) -> vec4<f32> {\n"
             "  let l: f32 = length(a.xyz);\n"
             "  let n: vec3<f32> = select(a.xyz, a.xyz / l, l > 0.0);\n"
             "  return vec4<f32>(n, n.z);\n"
             "}\n";
        s << "fn divsq(a: vec4<f32>, b: f32) -> vec4<f32> {\n"
             "  let r: vec4<f32> = a / sqrt(abs(b));\n"
             "  return select(r, vec4<f32>(0.0), a == vec4<f32>(0.0));\n"
             "}\n";
        s << "fn lin2srgb(c: vec4<f32>) -> vec4<f32> {\n"
             "  let x: vec3<f32> = clamp(c.rgb, vec3<f32>(0.0), vec3<f32>(1.0));\n"
             "  let r: vec3<f32> = select(1.055 * pow(x, vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055), 12.92 * x, x < vec3<f32>(0.0031308));\n"
             "  return vec4<f32>(r, c.a);\n"
             "}\n";
        s << "fn srgb2lin(c: vec4<f32>) -> vec4<f32> {\n"
             "  let x: vec3<f32> = c.rgb;\n"
             "  let r: vec3<f32> = select(pow((x + vec3<f32>(0.055)) / vec3<f32>(1.055), vec3<f32>(2.4)), x / 12.92, x <= vec3<f32>(0.04045));\n"
             "  return vec4<f32>(r, c.a);\n"
             "}\n";
        s << "fn program(inp: FsIn) {\n" << body.str() << "}\n";

        std::string outs[4];
        bool full = (key_.control & 0x40) != 0;
        const uint32_t out_idx_r[4] = {0, 2, 3, 4};
        const uint32_t out_idx_h[4] = {0, 4, 6, 8};
        for (uint32_t m = 0; m < 4; m++) outs[m] = reg_name(!full, full ? out_idx_r[m] : out_idx_h[m]);

        s << "@fragment\nfn fs_main(inp: FsIn) -> FsOut {\n";
        s << "  g_frag_coord = inp.position;\n  g_front_facing = inp.front_facing;\n";
        s << "  CC[0] = vec4<f32>(0.0);\n  CC[1] = vec4<f32>(0.0);\n";
        s << "  program(inp);\n";
        s << "  let c0: vec4<f32> = " << outs[0] << ";\n";
        s << "  if ((fstate.fflags.x & 1u) != 0u) {\n"
             "    let f: u32 = fstate.fflags.z & 7u;\n"
             "    let a: f32 = c0.a;\n"
             "    let r: f32 = fstate.window.w;\n"
             "    var alpha_pass: bool = true;\n"
             "    if (f == 0u) { alpha_pass = false; } else if (f == 1u) { alpha_pass = a < r; } else if (f == 2u) { alpha_pass = a == r; }\n"
             "    else if (f == 3u) { alpha_pass = a <= r; } else if (f == 4u) { alpha_pass = a > r; } else if (f == 5u) { alpha_pass = a != r; }\n"
             "    else if (f == 6u) { alpha_pass = a >= r; }\n"
             "    if (!alpha_pass) { discard; }\n"
             "  }\n";
        s << "  var out: FsOut;\n";
        for (uint32_t m = 0; m < mrt; m++) {
            std::string v = m == 0 ? "c0" : outs[m];
            s << "  out.color" << m << " = " << sel(v, "lin2srgb(" + v + ")", "((fstate.fflags.x & 2u) != 0u) && " + (full ? std::string("false") : std::string("true")))
              << ";\n";
        }
        /* The GLSL sets out_color0 twice unconditionally, in this order; the last
         * write wins, so the WGSL folds them into selects rather than emitting
         * three assignments that only one of which is meant. */
        s << "  out.color0 = " << sel("out.color0", "vec4<f32>(1.0, 0.0, 1.0, 1.0)", "(fstate.fflags.w & 1u) != 0u") << ";\n";
        s << "  out.color0 = " << sel("out.color0", "vec4<f32>(c0.rgb, 1.0)", "(fstate.fflags.w & 2u) != 0u") << ";\n";
        if (depth) s << "  out.frag_depth = r1.z;\n";
        s << "  return out;\n}\n";

        out.source = s.str();
        out.texture_mask = used_tex_;
        out.hash = key_.hash;
        out.literal_count = static_cast<uint32_t>(info_.literal_slots.size());
        out.valid = true;
        return out;
    }

private:
    const FpWordReader& read_;
    FragmentProgramKey key_;
    FragmentProgramInfo info_;
    uint32_t current_literal_ = 0;
    uint32_t used_tex_ = 0;
    std::set<std::string> regs_;
    std::set<uint32_t> conflict_;

    bool aliased(bool half, uint32_t idx) const { return half && conflict_.count(idx >> 1); }

    std::string reg_name(bool half, uint32_t idx) {
        if (aliased(half, idx)) {
            std::string base = "r" + std::to_string(idx >> 1);
            regs_.insert(base);
            return "hget(" + base + ", " + std::to_string(idx & 1) + ")";
        }
        std::string n = (half ? "h" : "r") + std::to_string(idx);
        regs_.insert(n);
        return n;
    }

    void scan_registers() {
        std::set<uint32_t> r_used, h_used;
        uint32_t p = 0;
        while (p < info_.length_slots) {
            FpSlot sl = fp_read_slot(read_, info_.start_slot + p);
            uint32_t op = sl.opcode();
            bool lit = sl.uses_literal() && (!fp_is_flow(op) || op == 0);
            if (!fp_is_flow(op) && op != 0) {
                uint32_t d = sl.w[0];
                if (!((d >> 30) & 1)) ((d >> 7) & 1 ? h_used : r_used).insert((d >> 1) & 0x3F);
                for (uint32_t n = 0; n < 3; n++) {
                    uint32_t w = sl.w[1 + n];
                    if ((w & 3) == 0) ((w >> 8) & 1 ? h_used : r_used).insert((w >> 2) & 0x3F);
                }
            }
            p += lit ? 2 : 1;
            if (sl.end()) break;
        }
        bool full = (key_.control & 0x40) != 0;
        const uint32_t outs_r[4] = {0, 2, 3, 4};
        const uint32_t outs_h[4] = {0, 4, 6, 8};
        for (uint32_t m = 0; m < 4; m++) (full ? r_used : h_used).insert(full ? outs_r[m] : outs_h[m]);
        if (key_.control & 0xE) r_used.insert(1);
        for (uint32_t r : r_used) {
            if (h_used.count(r * 2) || h_used.count(r * 2 + 1)) conflict_.insert(r);
        }
    }

    uint32_t dim(uint32_t unit) const { return (key_.texture_dims >> (unit * 2)) & 3; }

    std::string input(uint32_t attr) {
        switch (attr) {
        case 0: return "wpos(inp)";
        case 1: return "clamp(select(inp.in_bfc0, inp.in_col0, g_front_facing), vec4<f32>(0.0), vec4<f32>(1.0))";
        case 2: return "clamp(select(inp.in_bfc1, inp.in_col1, g_front_facing), vec4<f32>(0.0), vec4<f32>(1.0))";
        case 3: return "fogc(inp)";
        case 14: return "vec4<f32>(select(-1.0, 1.0, g_front_facing))";
        default:
            if (attr >= 4 && attr <= 13) return "inp.in_tex" + std::to_string(attr - 4);
            return "vec4<f32>(0.0)";
        }
    }

    std::string swizzle(const std::string& base, uint32_t raw) {
        return "(" + base + ")." + lane(raw >> 9) + lane(raw >> 11) + lane(raw >> 13) + lane(raw >> 15);
    }

    std::string src(const FpSlot& sl, uint32_t n) {
        uint32_t w = sl.w[1 + n];
        uint32_t type = w & 3;
        std::string v;
        if (type == 0) {
            v = swizzle(reg_name((w >> 8) & 1, (w >> 2) & 0x3F), w);
        } else if (type == 1) {
            v = swizzle(input((sl.w[0] >> 13) & 0xF), w);
        } else if (type == 2) {
            v = swizzle("fcvals.fc[" + std::to_string(current_literal_) + "]", w);
        } else {
            v = "vec4<f32>(0.0)";
        }
        uint32_t prec = (sl.w[2] >> (19 + 3 * n)) & 7;
        bool absf = n == 0 ? (sl.w[1] >> 29) & 1 : (w >> 18) & 1;
        if (prec == 4) v = "clamp(" + v + ", vec4<f32>(0.0), vec4<f32>(1.0))";
        if (absf) v = "abs(" + v + ")";
        if (prec == 2) v = "clamp(" + v + ", vec4<f32>(-2.0), vec4<f32>(2.0))";
        else if (prec == 3) v = "clamp(" + v + ", vec4<f32>(-1.0), vec4<f32>(1.0))";
        if ((w >> 17) & 1) v = "(-" + v + ")";
        return v;
    }

    std::string tex_coord(const std::string& a, uint32_t unit) {
        bool un = (key_.unnormalized >> unit) & 1;
        std::string size = "textureDimensions(tex" + std::to_string(unit) + ")";
        std::string ts = "fstate.texscale[" + std::to_string(unit >> 2) + "][" + std::to_string(unit & 3) + "]";
        switch (dim(unit)) {
        case 0: return un ? "(" + a + ".x / (f32(" + size + ") / " + ts + "))" : "(" + a + ").x";
        case 1: return un ? "(" + a + ".xy / (vec2<f32>(" + size + ") / " + ts + "))" : "(" + a + ").xy";
        default: return un && dim(unit) == 3 ? "(" + a + ".xyz / (vec3<f32>(" + size + ") / " + ts + "))" : "(" + a + ").xyz";
        }
    }

    std::string tex(const FpSlot& sl, uint32_t op) {
        uint32_t unit = (sl.w[0] >> 17) & 0xF;
        used_tex_ |= 1u << unit;
        std::string t = "tex" + std::to_string(unit);
        std::string sp = "smp" + std::to_string(unit);
        std::string a = src(sl, 0);
        std::string r;
        if (((key_.shadow >> unit) & 1) && dim(unit) == 1) {
            std::string c = op == 0x18 ? "(" + a + " / " + a + ".w)" : a;
            std::string uv = tex_coord(c, unit);
            /* Shadow comparison returns a scalar in GLSL; WGSL spells the three
             * forms separately and has no gradient variant. */
            switch (op) {
            case 0x17:
            case 0x18: r = "vec4<f32>(textureSampleCompare(" + t + ", " + sp + ", " + uv + ", (" + c + ").z))"; break;
            case 0x19: r = "vec4<f32>(textureSampleCompareLevel(" + t + ", " + sp + ", " + uv + ", (" + c + ").z))"; break;
            case 0x2F: r = "vec4<f32>(textureSampleCompareLevel(" + t + ", " + sp + ", " + uv + ", (" + c + ").z))"; break;
            case 0x31: r = "vec4<f32>(textureSampleCompareLevel(" + t + ", " + sp + ", " + uv + ", (" + c + ").z))"; break;
            default: r = "vec4<f32>(0.0)"; break;
            }
            if ((sl.w[0] >> 21) & 1) r = "(" + r + " * 2.0 - 1.0)";
            return r;
        }
        switch (op) {
        case 0x17: r = "textureSample(" + t + ", " + sp + ", " + tex_coord(a, unit) + ")"; break;
        case 0x18: r = "textureSample(" + t + ", " + sp + ", " + tex_coord("(" + a + " / " + a + ".w)", unit) + ")"; break;
        case 0x19:
            r = "textureSampleGrad(" + t + ", " + sp + ", " + tex_coord(a, unit) + ", " + tex_coord(src(sl, 1), unit) + ", " +
                tex_coord(src(sl, 2), unit) + ")";
            break;
        case 0x2F: r = "textureSampleLevel(" + t + ", " + sp + ", " + tex_coord(a, unit) + ", " + src(sl, 1) + ".x)"; break;
        case 0x31: r = "textureSample(" + t + ", " + sp + ", " + tex_coord(a, unit) + ", " + src(sl, 1) + ".x)"; break;
        default: r = "vec4<f32>(0.0)"; break;
        }
        if ((sl.w[0] >> 21) & 1) r = "(" + r + " * 2.0 - 1.0)";
        return r;
    }

    std::string expr(const FpSlot& sl, uint32_t op) {
        auto a = [&] { return src(sl, 0); };
        auto b = [&] { return src(sl, 1); };
        auto c = [&] { return src(sl, 2); };
        switch (op) {
        case 0x01: return a();
        case 0x02: return a() + " * " + b();
        case 0x03: return a() + " + " + b();
        case 0x04: return "fma(" + a() + ", " + b() + ", " + c() + ")";
        case 0x05: return "vec4<f32>(dot(" + a() + ".xyz, " + b() + ".xyz))";
        case 0x06: return "vec4<f32>(dot(" + a() + ", " + b() + "))";
        case 0x07: return "vec4<f32>(1.0, " + a() + ".y * " + b() + ".y, " + a() + ".z, " + b() + ".w)";
        case 0x08: return "min(" + a() + ", " + b() + ")";
        case 0x09: return "max(" + a() + ", " + b() + ")";
        case 0x0A: return "select(vec4<f32>(0.0), vec4<f32>(1.0), " + a() + " < " + b() + ")";
        case 0x0B: return "select(vec4<f32>(0.0), vec4<f32>(1.0), " + a() + " >= " + b() + ")";
        case 0x0C: return "select(vec4<f32>(0.0), vec4<f32>(1.0), " + a() + " <= " + b() + ")";
        case 0x0D: return "select(vec4<f32>(0.0), vec4<f32>(1.0), " + a() + " > " + b() + ")";
        case 0x0E: return "select(vec4<f32>(0.0), vec4<f32>(1.0), " + a() + " != " + b() + ")";
        case 0x0F: return "select(vec4<f32>(0.0), vec4<f32>(1.0), " + a() + " == " + b() + ")";
        case 0x10: return "fract(" + a() + ")";
        case 0x11: return "floor(" + a() + ")";
        case 0x13: return "vec4<f32>(bitcast<f32>(pack4x8snorm(" + a() + ")))";
        case 0x14: return "unpack4x8snorm(bitcast<u32>(" + a() + ".x))";
        case 0x15: return "dpdx(" + a() + ")";
        case 0x16: return "dpdy(" + a() + ")";
        case 0x17: case 0x18: case 0x19: case 0x2F: case 0x31: return tex(sl, op);
        case 0x1A: return "vec4<f32>(1.0 / " + a() + ".x)";
        case 0x1B: return "vec4<f32>(inverseSqrt(abs(" + a() + ".x)))";
        case 0x1C: return "vec4<f32>(exp2(" + a() + ".x))";
        case 0x1D: return "vec4<f32>(log2(" + a() + ".x))";
        case 0x1E: return "lit(" + a() + ")";
        case 0x1F: return "mix(" + c() + ", " + b() + ", " + a() + ")";
        case 0x20: return "vec4<f32>(1.0)";
        case 0x21: return "vec4<f32>(0.0)";
        case 0x22: return "vec4<f32>(cos(" + a() + ".x))";
        case 0x23: return "vec4<f32>(sin(" + a() + ".x))";
        case 0x24: return "vec4<f32>(bitcast<f32>(pack2x16float(" + a() + ".xy)))";
        case 0x25: return "unpack2x16float(bitcast<u32>(" + a() + ".x)).xyxy";
        case 0x26: return "exp2(" + b() + " * log2(" + a() + "))";
        case 0x27: return "vec4<f32>(bitcast<f32>(pack4x8unorm(" + a() + ")))";
        case 0x28: return "unpack4x8unorm(bitcast<u32>(" + a() + ".x))";
        case 0x29: return "vec4<f32>(bitcast<f32>(pack2x16unorm(" + a() + ".xy)))";
        case 0x2A: return "unpack2x16unorm(bitcast<u32>(" + a() + ".x)).xyxy";
        case 0x2B: return a() + ".xyxy + " + b() + ".xxxx * " + c() + ".xzxz + " + b() + ".yyyy * " + c() + ".ywyw";
        case 0x2C: return "vec4<f32>(bitcast<f32>(pack4x8unorm(lin2srgb(" + a() + "))))";
        case 0x2D: return "srgb2lin(unpack4x8unorm(bitcast<u32>(" + a() + ".x)))";
        case 0x2E: return "vec4<f32>(dot(" + a() + ".xy, " + b() + ".xy) + " + c() + ".x)";
        case 0x36: return "reflect(" + a() + ", " + b() + ")";
        case 0x38: return "vec4<f32>(dot(" + a() + ".xy, " + b() + ".xy))";
        case 0x39: return "nrm(" + a() + ")";
        case 0x3A: return a() + " / " + b() + ".x";
        case 0x3B: return "divsq(" + a() + ", " + b() + ".x)";
        case 0x3C: return "vec4<f32>(1.0, " + a() + ".y, select(0.0, exp2(" + a() + ".w), " + a() + ".y > 0.0), 1.0)";
        default: return "";
        }
    }

    std::string cc_test(const FpSlot& sl) {
        uint32_t s0 = sl.w[1];
        uint32_t bits = (s0 >> 18) & 7;
        uint32_t sel_ = (s0 >> 31) & 1;
        std::string sw = std::string(".") + lane(s0 >> 21) + lane(s0 >> 23) + lane(s0 >> 25) + lane(s0 >> 27);
        return "cctest(CC[" + std::to_string(sel_) + "]" + sw + ", " + std::to_string(bits) + "u)";
    }

    void emit_op(std::ostringstream& o, const FpSlot& sl, uint32_t op, const std::string& ind) {
        uint32_t bits = (sl.w[1] >> 18) & 7;
        if (bits == 0) return;
        bool cond = bits != 7;
        if (op == 0x12) {
            o << ind << (cond ? "if (any(" + cc_test(sl) + ")) { discard; }\n" : "discard;\n");
            return;
        }
        if (op == 0x3D || op == 0x3E) return;
        std::string e = expr(sl, op);
        if (e.empty()) {
            o << ind << "// unsupported opcode 0x" << std::hex << op << std::dec << "\n";
            return;
        }
        uint32_t d = sl.w[0];
        uint32_t scale = (sl.w[2] >> 28) & 7;
        static const char* scales[8] = {"", " * 2.0", " * 4.0", " * 8.0", "", " * 0.5", " * 0.25", " * 0.125"};
        o << ind << "{\n";
        std::string in = ind + "  ";
        o << in << "var t: vec4<f32> = (" << e << ")" << scales[scale] << ";\n";
        bool fp16 = (d >> 7) & 1;
        if (fp16) o << in << "t = clamp(t, vec4<f32>(-65504.0), vec4<f32>(65504.0));\n";
        uint32_t prec = (d >> 22) & 3;
        if ((d >> 31) & 1) o << in << "t = sat(t);\n";
        else if (prec == 2) o << in << "t = clamp(t, vec4<f32>(-2.0), vec4<f32>(2.0));\n";
        else if (prec == 3) o << in << "t = clamp(t, vec4<f32>(-1.0), vec4<f32>(1.0));\n";
        if (cond) o << in << "let ct: vec4<bool> = " << cc_test(sl) << ";\n";
        uint32_t mask = (d >> 9) & 0xF;
        bool no_dest = (d >> 30) & 1;
        uint32_t dst_idx = (d >> 1) & 0x3F;
        bool alias_dst = !no_dest && aliased(fp16, dst_idx);
        std::string dst = alias_dst ? "hv" : reg_name(fp16, dst_idx);
        if (alias_dst) o << in << "var hv: vec4<f32> = " << reg_name(true, dst_idx) << ";\n";
        bool set_cc = (d >> 8) & 1;
        std::string cc = "CC[" + std::to_string((sl.w[1] >> 30) & 1) + "]";
        for (uint32_t l = 0; l < 4; l++) {
            if (!(mask & (1u << l))) continue;
            std::string guard = cond ? std::string("if (ct.") + lane(l) + ") { " : "";
            std::string close = cond ? " }" : "";
            if (!no_dest) o << in << guard << dst << "." << lane(l) << " = t." << lane(l) << ";" << close << "\n";
            if (set_cc) o << in << guard << cc << "." << lane(l) << " = t." << lane(l) << ";" << close << "\n";
        }
        if (alias_dst) {
            std::string base = "r" + std::to_string(dst_idx >> 1);
            o << in << base << " = hset(" << base << ", " << (dst_idx & 1) << ", hv);\n";
        }
        o << ind << "}\n";
    }

    void emit_flow(std::ostringstream& o, const FpSlot& sl, uint32_t op, uint32_t p, std::string& ind,
                   std::vector<std::pair<uint32_t, std::string>>& closers) {
        uint32_t bits = (sl.w[1] >> 18) & 7;
        std::string test = bits == 7 ? "true" : bits == 0 ? "false" : "any(" + cc_test(sl) + ")";
        switch (op) {
        case 0x42: {
            uint32_t else_slot = (sl.w[2] & 0x7FFFFFFF) >> 2;
            uint32_t end_slot = sl.w[3] >> 2;
            o << ind << "if (" << test << ") {\n";
            ind += "  ";
            closers.push_back({end_slot, "}"});
            if (else_slot != end_slot) closers.push_back({else_slot, "} else {"});
            break;
        }
        case 0x43:
        case 0x44: {
            uint32_t count = (sl.w[2] >> 2) & 0xFF;
            int32_t init = (sl.w[2] >> 10) & 0xFF;
            int32_t inc = static_cast<int8_t>((sl.w[2] >> 19) & 0xFF);
            uint32_t end_slot = sl.w[3] >> 2;
            std::string lc = "lc" + std::to_string(p);
            std::string it = "it" + std::to_string(p);
            /* WGSL's for-init declares one variable; the RSX loop carries two. The
             * counter is derived from the induction variable each iteration, which
             * is the same sequence the GLSL increments produce. */
            o << ind << "if (" << test << ") {\n";
            ind += "  ";
            o << ind << "for (var " << it << ": i32 = 0; " << it << " < " << count << "; " << it << " = " << it << " + 1) {\n";
            ind += "  ";
            o << ind << "let " << lc << ": i32 = " << init << " + " << it << " * " << inc << ";\n";
            closers.push_back({end_slot, "}"});
            closers.push_back({end_slot, "}"});
            break;
        }
        case 0x40: o << ind << "if (" << test << ") { break; }\n"; break;
        case 0x45: o << ind << "if (" << test << ") { return; }\n"; break;
        default: o << ind << "// unsupported flow opcode 0x" << std::hex << op << std::dec << "\n"; break;
        }
    }
};

}  // namespace

WgslShader emit_vertex_wgsl(const uint32_t* ucode, uint32_t start_slot) {
    if (!ucode || start_slot >= VP_SLOTS) return {};
    if (!vp_program_is_sane(ucode, start_slot, VP_SLOTS)) {
        LOG_WARN("wgsl", "vp slot %u: no end instruction, refusing to emit", start_slot);
        return {};
    }
    try {
        return VertexEmitter(ucode, start_slot).run();
    } catch (const std::exception& e) {
        LOG_WARN("wgsl", "vertex emission threw: %s", e.what());
        return {};
    }
}

WgslShader emit_fragment_wgsl(const FpWordReader& read, const FragmentProgramKey& key) {
    FragmentProgramInfo info = analyse_fragment_program(read);
    if (!info.valid) return {};
    WgslShader out = FragmentEmitter(read, key, info).run();
    out.hash = key.hash;
    return out;
}

}  // namespace gpu
