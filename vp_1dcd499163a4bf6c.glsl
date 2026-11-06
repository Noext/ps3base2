#version 450
layout(location = 0) in vec4 v0;
layout(std140, set = 0, binding = 0) uniform VertexConstants { vec4 c[468]; };
layout(std140, set = 0, binding = 1) uniform VertexState { vec4 vp_scale; vec4 vp_offset; vec4 surface; uvec4 flags; };
layout(location = 1) out vec4 out_col0;
layout(location = 2) out vec4 out_col1;
layout(location = 3) out vec4 out_fog;
layout(location = 4) out vec4 out_tex0;
layout(location = 5) out vec4 out_tex1;
layout(location = 6) out vec4 out_tex2;
layout(location = 7) out vec4 out_tex3;
layout(location = 8) out vec4 out_tex4;
layout(location = 9) out vec4 out_tex5;
layout(location = 10) out vec4 out_tex6;
layout(location = 11) out vec4 out_tex7;
layout(location = 12) out vec4 out_tex8;
layout(location = 13) out vec4 out_tex9;
layout(location = 14) out vec4 out_bfc0;
layout(location = 15) out vec4 out_bfc1;
out gl_PerVertex { invariant vec4 gl_Position; float gl_PointSize; };
vec4 cread(int i) { return (i >= 0 && i < 468) ? c[i] : vec4(0.0); }
bvec4 cctest(vec4 v, int cond) {
    bvec4 lt = lessThan(v, vec4(0.0)); bvec4 eq = equal(v, vec4(0.0)); bvec4 gt = greaterThan(v, vec4(0.0));
    bvec4 r = bvec4(false);
    if ((cond & 1) != 0) r = bvec4(uvec4(r) | uvec4(lt));
    if ((cond & 2) != 0) r = bvec4(uvec4(r) | uvec4(eq));
    if ((cond & 4) != 0) r = bvec4(uvec4(r) | uvec4(gt));
    return r;
}
void main() {
    vec4 R[32];
    for (int i = 0; i < 32; i++) R[i] = vec4(0.0);
    vec4 o[16];
    for (int i = 0; i < 16; i++) o[i] = vec4(0.0, 0.0, 0.0, 1.0);
    ivec4 A[2] = ivec4[2](ivec4(0), ivec4(0));
    vec4 CC[2] = vec4[2](vec4(0.0), vec4(0.0));
    {
        vec4 vr = (cread(3)).xyzw;
        o[8].x = vr.x;
        o[8].y = vr.y;
        o[8].z = vr.z;
        o[8].w = vr.w;
    }
    {
        vec4 vr = (cread(128)).yyyx;
        o[7].z = vr.z;
        o[7].w = vr.w;
    }
    {
        vec4 vr = (cread(0)).zzzz;
        o[0].z = vr.z;
    }
    {
        vec4 vr = (cread(128)).yyyy;
        o[0].w = vr.w;
    }
    {
        vec4 vr = vec4(dot((v0).xyzw, (cread(4)).xyzw));
        o[7].x = vr.x;
    }
    {
        vec4 vr = vec4(dot((v0).xyzw, (cread(5)).xyzw));
        o[7].y = vr.y;
    }
    {
        vec4 vr = vec4(dot((v0).xyzw, (cread(0)).xyzw));
        R[0].x = vr.x;
    }
    {
        vec4 vr = vec4(dot((v0).xyzw, (cread(1)).xyzw));
        R[0].y = vr.y;
    }
    {
        vec4 vr = fma((R[0]).xyxx, (cread(6)).xyxx, (cread(6)).zwzz);
        o[0].x = vr.x;
        o[0].y = vr.y;
    }
    uint omask = flags.x;
    out_col0 = (omask & 1u) != 0u ? o[1] : vec4(0.0, 0.0, 0.0, 1.0);
    out_col1 = (omask & 2u) != 0u ? o[2] : vec4(0.0, 0.0, 0.0, 1.0);
    bool two_side = (flags.y & 1u) != 0u;
    out_bfc0 = two_side && (omask & 4u) != 0u ? o[3] : out_col0;
    out_bfc1 = two_side && (omask & 8u) != 0u ? o[4] : out_col1;
    out_fog = (omask & 16u) != 0u ? o[5].xxxx : vec4(0.0);
    out_tex0 = (omask & 16384u) != 0u ? o[7] : vec4(0.0, 0.0, 0.0, 1.0);
    out_tex1 = (omask & 32768u) != 0u ? o[8] : vec4(0.0, 0.0, 0.0, 1.0);
    out_tex2 = (omask & 65536u) != 0u ? o[9] : vec4(0.0, 0.0, 0.0, 1.0);
    out_tex3 = (omask & 131072u) != 0u ? o[10] : vec4(0.0, 0.0, 0.0, 1.0);
    out_tex4 = (omask & 262144u) != 0u ? o[11] : vec4(0.0, 0.0, 0.0, 1.0);
    out_tex5 = (omask & 524288u) != 0u ? o[12] : vec4(0.0, 0.0, 0.0, 1.0);
    out_tex6 = (omask & 1048576u) != 0u ? o[13] : vec4(0.0, 0.0, 0.0, 1.0);
    out_tex7 = (omask & 2097152u) != 0u ? o[14] : vec4(0.0, 0.0, 0.0, 1.0);
    out_tex8 = (omask & 4096u) != 0u ? o[15] : vec4(0.0, 0.0, 0.0, 1.0);
    out_tex9 = (omask & 8192u) != 0u ? o[6] : vec4(0.0, 0.0, 0.0, 1.0);
    gl_PointSize = (omask & 32u) != 0u ? o[6].x : uintBitsToFloat(flags.z);
    vec4 p = o[0];
    vec2 xy = (vp_scale.xy * p.xy + (vp_offset.xy - surface.xy * 0.5) * p.w) * surface.zw * 2.0;
    gl_Position = vec4(xy, vp_scale.z * p.z + vp_offset.z * p.w, p.w);
}

/*
0: 401f9c6c 0040300d 8186c083 6041ffa0
1: 401f9c6c 0048002a 0186c083 60407f9c
2: 401f9c6c 00400055 0186c083 60405f80
3: 401f9c6c 0048002a 8186c083 60403f80
4: 401f9c6c 01c0400d 8106c0c3 60411f9c
5: 401f9c6c 01c0500d 8106c0c3 60409f9c
6: 00001c6c 01c0000d 8106c0c3 60411ffc
7: 00001c6c 01c0100d 8106c0c3 60409ffc
8: 401f9c6c 01006008 008400d7 40619f81

*/
