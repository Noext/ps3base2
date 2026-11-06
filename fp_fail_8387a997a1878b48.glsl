#version 450
layout(location = 1) in vec4 in_col0;
layout(location = 2) in vec4 in_col1;
layout(location = 3) in vec4 in_fog;
layout(location = 4) in vec4 in_tex0;
layout(location = 5) in vec4 in_tex1;
layout(location = 6) in vec4 in_tex2;
layout(location = 7) in vec4 in_tex3;
layout(location = 8) in vec4 in_tex4;
layout(location = 9) in vec4 in_tex5;
layout(location = 10) in vec4 in_tex6;
layout(location = 11) in vec4 in_tex7;
layout(location = 12) in vec4 in_tex8;
layout(location = 13) in vec4 in_tex9;
layout(location = 14) in vec4 in_bfc0;
layout(location = 15) in vec4 in_bfc1;
layout(std140, set = 0, binding = 2) uniform FragmentConstants { vec4 fc[32]; };
layout(std140, set = 0, binding = 3) uniform FragmentState { vec4 window; vec4 fog; uvec4 fflags; vec4 texscale[4]; };
layout(set = 0, binding = 32) uniform sampler2D tex0;
layout(location = 0) out vec4 out_color0;
vec4 r0 = vec4(0.0);
vec4 r1 = vec4(0.0);
vec4 r2 = vec4(0.0);
vec4 r3 = vec4(0.0);
vec4 r4 = vec4(0.0);
vec4 hget(vec4 r, int hi) { vec2 a = hi == 0 ? r.xy : r.zw; return vec4(unpackHalf2x16(floatBitsToUint(a.x)), unpackHalf2x16(floatBitsToUint(a.y))); }
vec4 hset(vec4 r, int hi, vec4 h) { vec2 p = vec2(uintBitsToFloat(packHalf2x16(h.xy)), uintBitsToFloat(packHalf2x16(h.zw))); if (hi == 0) r.xy = p; else r.zw = p; return r; }
vec4 CC[2];
vec4 sat(vec4 v) { return clamp(mix(v, vec4(0.0), isnan(v)), 0.0, 1.0); }
bvec4 cctest(vec4 v, uint b) {
    bvec4 r = bvec4(false);
    if ((b & 1u) != 0u) r = bvec4(uvec4(r) | uvec4(lessThan(v, vec4(0.0))));
    if ((b & 2u) != 0u) r = bvec4(uvec4(r) | uvec4(equal(v, vec4(0.0))));
    if ((b & 4u) != 0u) r = bvec4(uvec4(r) | uvec4(greaterThan(v, vec4(0.0))));
    return r;
}
vec4 wpos() {
    vec4 f = gl_FragCoord;
    f.xy /= max(fog.z, 1.0);
    float y = window.y > 0.5 ? window.x - f.y : f.y;
    vec2 p = vec2(f.x, y) - (window.z > 0.5 ? vec2(0.5) : vec2(0.0));
    return vec4(p, f.z, f.w);
}
vec4 fogc() {
    float c = in_fog.x;
    uint mode = fflags.y;
    float a = (mode >= 0x802u) ? abs(c) : c;
    float y;
    if (mode == 0x2601u || mode == 0x804u) y = fog.y * a + (fog.x - 1.0);
    else if (mode == 0x800u || mode == 0x802u) y = exp(11.084 * (fog.y * a + fog.x - 1.5));
    else { float k = 4.709 * (fog.y * a + fog.x - 1.5); y = exp(-(k * k)); }
    return vec4(c, clamp(y, 0.0, 1.0), 0.0, 0.0);
}
vec4 lit(vec4 a) { float x = max(a.x, 0.0); return vec4(1.0, x, x > 0.0 ? pow(max(a.y, 1e-10), a.w) : 0.0, 1.0); }
vec4 nrm(vec4 a) { float l = length(a.xyz); vec3 n = l > 0.0 ? a.xyz / l : a.xyz; return vec4(n, n.z); }
vec4 divsq(vec4 a, float b) { vec4 r = a / sqrt(abs(b)); return mix(r, vec4(0.0), equal(a, vec4(0.0))); }
vec4 lin2srgb(vec4 c) { vec3 x = clamp(c.rgb, 0.0, 1.0); vec3 r = mix(1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, 12.92 * x, lessThan(x, vec3(0.0031308))); return vec4(r, c.a); }
vec4 srgb2lin(vec4 c) { vec3 x = c.rgb; vec3 r = mix(pow((x + 0.055) / 1.055, vec3(2.4)), x / 12.92, lessThanEqual(x, vec3(0.04045))); return vec4(r, c.a); }
void program() {
    {
        vec4 t = ((in_tex0).xyzw);
        r1.x = t.x;
        r1.y = t.y;
    }
    {
        vec4 t = ((r1).xyzw + (fc[0]).xyxx);
        r0.x = t.x;
        r0.y = t.y;
    }
    {
        vec4 t = (min((r0).xxxy, (fc[1]).xyzw));
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = ((r1).xyzw + (fc[2]).xyxx);
        r0.x = t.x;
        r0.y = t.y;
    }
    {
        vec4 t = (min((r0).xyzw, (fc[3]).zwzz));
        r0.x = t.x;
        r0.y = t.y;
    }
    {
        vec4 t = ((r1).xxxy + (fc[4]).xxxy);
        r1.z = t.z;
        r1.w = t.w;
    }
    {
        vec4 t = (max((r0).xxxy, (fc[5]).xxxy));
        r3.z = t.z;
        r3.w = t.w;
    }
    {
        vec4 t = (max((r0).zwzz, (fc[6]).xyxx));
        r3.x = t.x;
        r3.y = t.y;
    }
    {
        vec4 t = (texture(tex0, (r3).zwzz.xy));
        r0.x = t.x;
        r0.y = t.y;
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = ((r0).xyzw * (fc[7]).zzzz);
        r2.x = t.x;
        r2.y = t.y;
        r2.z = t.z;
        r2.w = t.w;
    }
    {
        vec4 t = (texture(tex0, (r3).xyzw.xy));
        r0.x = t.x;
        r0.y = t.y;
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = (fma((r0).xyzw, (fc[8]).zzzz, (r2).xyzw));
        r0.x = t.x;
        r0.y = t.y;
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = (min((r1).zwzz, (fc[9]).zwzz));
        r2.x = t.x;
        r2.y = t.y;
    }
    {
        vec4 t = ((r1).xxxy + (fc[10]).xxxy);
        r1.z = t.z;
        r1.w = t.w;
    }
    {
        vec4 t = (min((r1).xyzw, (fc[11]).xyzw));
        r1.z = t.z;
        r1.w = t.w;
    }
    {
        vec4 t = (max((r2).xyzw, (fc[12]).xyxx));
        r2.x = t.x;
        r2.y = t.y;
    }
    {
        vec4 t = (texture(tex0, (r2).xyzw.xy));
        r2.x = t.x;
        r2.y = t.y;
        r2.z = t.z;
        r2.w = t.w;
    }
    {
        vec4 t = (fma((r2).xyzw, (fc[13]).zzzz, (r0).xyzw));
        r2.x = t.x;
        r2.y = t.y;
        r2.z = t.z;
        r2.w = t.w;
    }
    {
        vec4 t = (max((r1).zwzz, (fc[14]).xyxx));
        r0.x = t.x;
        r0.y = t.y;
    }
    {
        vec4 t = (texture(tex0, (r0).xyzw.xy));
        r0.x = t.x;
        r0.y = t.y;
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = (fma((r0).xyzw, (fc[15]).zzzz, (r2).xyzw));
        r2.x = t.x;
        r2.y = t.y;
        r2.z = t.z;
        r2.w = t.w;
    }
    {
        vec4 t = ((r1).xyzw + (fc[16]).xyxx);
        r0.x = t.x;
        r0.y = t.y;
    }
    {
        vec4 t = (min((r0).xxxy, (fc[17]).xyzw));
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = ((r1).xxxy + (fc[18]).xxxy);
        r1.z = t.z;
        r1.w = t.w;
    }
    {
        vec4 t = (max((r0).xyzw, (fc[19]).xxxy));
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = (texture(tex0, (r0).zwzz.xy));
        r0.x = t.x;
        r0.y = t.y;
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = (fma((r0).xyzw, (fc[20]).zzzz, (r2).xyzw));
        r0.x = t.x;
        r0.y = t.y;
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = (min((r1).zwzz, (fc[21]).zwzz));
        r2.x = t.x;
        r2.y = t.y;
    }
    {
        vec4 t = ((r1).xxxy + (fc[22]).xxxy);
        r1.z = t.z;
        r1.w = t.w;
    }
    {
        vec4 t = (min((r1).xyzw, (fc[23]).xyzw));
        r1.z = t.z;
        r1.w = t.w;
    }
    {
        vec4 t = ((r1).xyzw + (fc[24]).xyxx);
        r1.x = t.x;
        r1.y = t.y;
    }
    {
        vec4 t = (max((r2).xyzw, (fc[25]).xyxx));
        r2.x = t.x;
        r2.y = t.y;
    }
    {
        vec4 t = (texture(tex0, (r2).xyzw.xy));
        r2.x = t.x;
        r2.y = t.y;
        r2.z = t.z;
        r2.w = t.w;
    }
    {
        vec4 t = (fma((r2).xyzw, (fc[26]).zzzz, (r0).xyzw));
        r0.x = t.x;
        r0.y = t.y;
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = (min((r1).xxxy, (fc[27]).xyzw));
        r2.z = t.z;
        r2.w = t.w;
    }
    {
        vec4 t = (max((r1).zwzz, (fc[28]).xyxx));
        r2.x = t.x;
        r2.y = t.y;
    }
    {
        vec4 t = (texture(tex0, (r2).xyzw.xy));
        r1.x = t.x;
        r1.y = t.y;
        r1.z = t.z;
        r1.w = t.w;
    }
    {
        vec4 t = (fma((r1).xyzw, (fc[29]).zzzz, (r0).xyzw));
        r1.x = t.x;
        r1.y = t.y;
        r1.z = t.z;
        r1.w = t.w;
    }
    {
        vec4 t = (max((r2).zwzz, (fc[30]).xyxx));
        r0.x = t.x;
        r0.y = t.y;
    }
    {
        vec4 t = (texture(tex0, (r0).xyzw.xy));
        r0.x = t.x;
        r0.y = t.y;
        r0.z = t.z;
        r0.w = t.w;
    }
    {
        vec4 t = (fma((r0).xyzw, (fc[31]).zzzz, (r1).xyzw));
        r0.x = t.x;
        r0.y = t.y;
        r0.z = t.z;
        r0.w = t.w;
    }
}
void main() {
    CC[0] = vec4(0.0); CC[1] = vec4(0.0);
    program();
    vec4 c0 = r0;
    if ((fflags.x & 1u) != 0u) {
        uint f = fflags.z & 7u; float a = c0.a; float r = window.w; bool pass = true;
        if (f == 0u) pass = false; else if (f == 1u) pass = a < r; else if (f == 2u) pass = a == r; else if (f == 3u) pass = a <= r;
        else if (f == 4u) pass = a > r; else if (f == 5u) pass = a != r; else if (f == 6u) pass = a >= r;
        if (!pass) discard;
    }
    out_color0 = ((fflags.x & 2u) != 0u && false) ? lin2srgb(c0) : c0;
    if ((fflags.w & 1u) != 0u) out_color0 = vec4(1.0, 0.0, 1.0, 1.0);
    if ((fflags.w & 2u) != 0u) out_color0 = vec4(c0.rgb, 1.0);
    out_color0 = texture(tex7, in_tex0.zwzz.x);
}

/*
ERROR: 0:311: 'tex7' : undeclared identifier 
ERROR: 0:311: '' : compilation terminated 
ERROR: 2 compilation errors.  No code generated.


*/
