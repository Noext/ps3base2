#version 450
layout(location = 0) in vec4 v0;
layout(location = 1) in vec4 v1;
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
        vec4 vr = (v1).yzxy * (cread(128)).wwzw;
        R[1].x = vr.x;
        R[1].y = vr.y;
        R[1].z = vr.z;
    }
    {
        vec4 vr = (cread(128)).xxxx;
        R[0].z = vr.z;
        R[0].w = vr.w;
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
        vec4 vr = (R[1]).yyyy * (cread(2)).wyww;
        R[0].x = vr.x;
        R[0].y = vr.y;
    }
    {
        vec4 vr = fma((R[1]).xxxx, (cread(2)).xzxx, (R[0]).yxyy);
        R[0].x = vr.x;
        R[0].y = vr.y;
    }
    {
        vec4 vr = (cread(128)).xxxx;
        o[7].w = vr.w;
    }
    {
        vec4 vr = vec4(dot((R[0]).xyzw, (cread(0)).xyzw));
        R[2].x = vr.x;
    }
    {
        vec4 vr = vec4(dot((R[0]).xyzw, (cread(1)).xyzw));
        R[2].y = vr.y;
    }
    {
        vec4 vr = (R[1]).xxxy * (R[1]).xxxy;
        R[0].z = vr.z;
        R[0].w = vr.w;
    }
    {
        vec4 vr = (R[2]).xyxx * (R[2]).xyxx;
        R[0].x = vr.x;
        R[0].y = vr.y;
    }
    {
        vec4 vr = (R[0]).xxxx + (R[0]).yyyy;
        R[0].x = vr.x;
    }
    {
        vec4 vr = (R[0]).zzzz + (R[0]).wwww;
        R[0].y = vr.y;
    }
    {
        vec4 vr = vec4(dot((v0).xyzw, (cread(4)).xyzw));
        vec4 sr = vec4(inversesqrt(abs((R[0]).xxxx.x)));
        o[7].x = vr.x;
        R[1].x = sr.x;
    }
    {
        vec4 vr = vec4(dot((v0).xyzw, (cread(5)).xyzw));
        vec4 sr = vec4(inversesqrt(abs((R[0]).yyyy.x)));
        o[7].y = vr.y;
        R[1].y = sr.y;
    }
    {
        vec4 vr = vec4(dot((v0).xyzw, (cread(0)).xyzw));
        R[0].x = vr.x;
    }
    {
        vec4 sr = vec4(1.0 / (R[1]).xxxx.x);
        CC[0].x = sr.x;
    }
    {
        vec4 vr = vec4(dot((v0).xyzw, (cread(1)).xyzw));
        vec4 sr = vec4(1.0 / (R[1]).yyyy.x);
        R[0].y = vr.y;
        R[1].y = sr.y;
    }
    {
        vec4 vr = (cread(128)).xxxx;
        R[0].z = vr.z;
    }
    {
        bvec4 ct = cctest(CC[0].xxxx, 5);
        vec4 vr = (R[1]).xxxx * (R[1]).yyyy;
        if (ct.z) R[0].z = vr.z;
    }
    {
        vec4 vr = fma((R[2]).xyxx, (R[0]).zzzz, (R[0]).xyxx);
        R[0].x = vr.x;
        R[0].y = vr.y;
    }
    {
        vec4 vr = (R[1]).zzzz;
        o[7].z = vr.z;
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
9: 401f9c6c 0040300d 8186c083 6041ffa0
10: 00009c6c 00880130 813ec0c3 6041dffc
11: 00001c6c 00480000 0186c083 60407ffc
12: 401f9c6c 00400055 0186c083 60405f80
13: 401f9c6c 0048002a 8186c083 60403f80
14: 00001c6c 0080202a 82b7c0c3 60419ffc
15: 00001c6c 01002000 028800c8 a0219ffc
16: 401f9c6c 00480000 0186c083 60403f9c
17: 00011c6c 01c0000d 8086c0c3 60411ffc
18: 00011c6c 01c0100d 8086c0c3 60409ffc
19: 00001c6c 00800000 82804143 60407ffc
20: 00001c6c 00800008 04840243 60419ffc
21: 00001c6c 00c00000 0086c08a a0211ffc
22: 00001c6c 00c00055 0086c09f e0209ffc
23: 401f9c6c 21c0400d 8106c0c0 0031009c
24: 401f9c6c 21c0500d 8106c0ca a028809c
25: 00001c6c 01c0000d 8106c0c3 60411ffc
26: 001fdc6c 1000000d 8106c080 00b01ffc
27: 00001c6c 11c0100d 8106c0ca a0a880fc
28: 00001c6c 00480000 0186c083 60405ffc
29: 00003400 00800000 02954143 60405ffc
30: 00001c6c 01000008 04aa8042 00219ffc
31: 401f9c6c 00400055 0286c083 60405f9c
32: 401f9c6c 01006008 008400d7 40619f81

*/
