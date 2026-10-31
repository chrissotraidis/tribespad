/* HaloPad shader translator (G4): Direct3D 9 shader bytecode (vs_1_1, vs_2_0, ps_1_1-1_4,
 * ps_2_0) -> Metal Shading Language.
 *
 * Every program becomes one MSL function over a fixed interface:
 *   vertex:   hp_vs(hp_vs_in [[stage_in]], constant hp_vs_constants &k [[buffer(16)]])
 *             inputs v<n> are [[attribute(n)]]; the pipeline maps declaration elements to them.
 *   fragment: hp_ps(hp_ps_in [[stage_in]], constant hp_ps_constants &k [[buffer(0)]],
 *             texture<n>/sampler<n> for each stage used)
 *   Varyings (hp_vs_out / hp_ps_in): position, colour 0-1, texture coordinates 0-7, fog, point size.
 * Direct3D rules reproduced here:
 *   - ps_1_x: constants are clamped to [-1, 1]; every result is clamped to
 *     [-PixelShader1xMaxValue, +PixelShader1xMaxValue] (8, the contract value) unless
 *     saturated; colour inputs are saturated; co-issued instruction pairs read their sources
 *     before either writes; the colour output is r0 (1.1-1.3) or r0 at the end (1.4).
 *   - Source modifiers (negate, bias, bx2, 1-x, x2, abs, dz/dw), destination shifts and
 *     _sat, write masks and swizzles; relative constant addressing through a0 (vs_1_1 mov
 *     to a0 floors, vs_2_0 mova rounds to nearest); out-of-range constants read as 0.
 *   - def constants take precedence over constants set through the device.
 *   - rsq and log use |x|; rcp/rsq of 0 give +inf; pow uses |x|; scalar instructions read
 *     .w when the source has no replicate swizzle; nrm scales all four components.
 *   - Fixed-function steps after the pixel shader (alpha test, fog blend) come from
 *     hp_ps_constants, so one compiled pipeline serves every value of those states.
 * Anything outside this set is refused with a message naming the opcode or register. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t sampler_dim[16];     /* ps_1_x: 2 = 2D, 3 = cube, 4 = volume, from the bound textures */
    uint8_t projected[16];       /* ps_1_x tex: divide coordinates by .w (D3DTTFF_PROJECTED) */
    uint8_t test_kernel;         /* emit a compute kernel over buffers instead (differential tests) */
} hp_shader_key;

typedef struct { char *s; size_t n, cap; char *err; size_t errlen; int failed; } out;

static void emit(out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char tmp[4096];
    int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    if (o->n + (size_t)k + 1 > o->cap) { o->cap = (o->cap + (size_t)k + 1) * 2; o->s = realloc(o->s, o->cap); }
    memcpy(o->s + o->n, tmp, (size_t)k + 1);
    o->n += (size_t)k;
}

static void fail(out *o, const char *fmt, ...)
{
    if (o->failed) return;
    o->failed = 1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(o->err, o->errlen, fmt, ap);
    va_end(ap);
}

enum { RT_TEMP = 0, RT_INPUT = 1, RT_CONST = 2, RT_ADDR = 3, RT_RASTOUT = 4, RT_ATTROUT = 5, RT_OUTPUT = 6, RT_CONSTINT = 7,
       RT_COLOROUT = 8, RT_DEPTHOUT = 9, RT_SAMPLER = 10, RT_CONSTBOOL = 14 };

typedef struct {
    int vs, major, minor;
    int ps1;                   /* pixel shader 1.x */
    int phase2;                /* ps_1_4 after the phase marker */
    uint8_t def_f[256];        /* constants defined by def */
    uint8_t sampler_dim[16];   /* 2 = 2D, 3 = cube, 4 = volume */
    uint8_t sampler_used[16];
    uint8_t input_used[16];    /* vs inputs v<n> declared */
    int writes_depth;
    const hp_shader_key *key;
} ctx;

static int regtype(uint32_t p) { return (int)(((p >> 28) & 7) | ((p >> 8) & 0x18)); }
static int regnum(uint32_t p) { return (int)(p & 0x7FF); }

static const char *comp = "xyzw";

static void swz(char *buf, uint32_t p)
{
    uint32_t s = (p >> 16) & 0xFF;
    buf[0] = '.';
    for (int i = 0; i < 4; i++) buf[1 + i] = comp[(s >> (2 * i)) & 3];
    buf[5] = 0;
}

/* A register as an MSL float4 expression (before swizzle and modifiers). */
static void reg_expr(ctx *c, out *o, char *buf, size_t size, uint32_t p, const uint32_t *rel)
{
    int t = regtype(p), n = regnum(p);
    switch (t) {
    case RT_TEMP: snprintf(buf, size, "r%d", n); return;
    case RT_INPUT:
        if (c->vs) snprintf(buf, size, "in.v%d", n);
        else snprintf(buf, size, "v%d", n);
        return;
    case RT_CONST:
        if (p & 0x2000) {                                           /* relative: c[a0.x + n] */
            const char *idx = "a0.x";
            char ibuf[32];
            if (rel) {
                uint32_t rs = (*rel >> 16) & 3;
                snprintf(ibuf, sizeof ibuf, "a0.%c", comp[rs]);
                idx = ibuf;
            }
            snprintf(buf, size, "hp_c(k, %s + %d)", idx, n);
            return;
        }
        if (c->def_f[n]) snprintf(buf, size, "d%d", n);
        else if (c->ps1) snprintf(buf, size, "clamp(k.c[%d], -1.0f, 1.0f)", n);
        else snprintf(buf, size, "k.c[%d]", n);
        return;
    case RT_ADDR:                /* vs: a0; ps_1_1-1_3: the texture register; ps_1_4/2_0: interpolated coordinates */
        if (c->vs) snprintf(buf, size, "float4(a0)");
        else if (c->ps1 && c->minor < 4) snprintf(buf, size, "t%d", n);
        else snprintf(buf, size, "tc%d", n);
        return;
    case RT_CONSTINT: snprintf(buf, size, "float4(k.i[%d])", n); return;
    case RT_CONSTBOOL: snprintf(buf, size, "float4(k.b[%d] ? 1.0f : 0.0f)", n); return;
    }
    fail(o, "source register type %d number %d", t, n);
    snprintf(buf, size, "float4(0)");
}

/* Source operand: register, swizzle, modifier. */
static void src(ctx *c, out *o, char *buf, size_t size, uint32_t p, const uint32_t *rel)
{
    char r[256], s[8];
    reg_expr(c, o, r, sizeof r, p, rel);
    swz(s, p);
    char base[300];
    snprintf(base, sizeof base, "%s%s", r, strcmp(s, ".xyzw") ? s : "");
    switch ((p >> 24) & 0xF) {
    case 0: snprintf(buf, size, "%s", base); return;
    case 1: snprintf(buf, size, "(-%s)", base); return;
    case 2: snprintf(buf, size, "(%s - 0.5f)", base); return;
    case 3: snprintf(buf, size, "(-(%s - 0.5f))", base); return;
    case 4: snprintf(buf, size, "(2.0f * %s - 1.0f)", base); return;
    case 5: snprintf(buf, size, "(-(2.0f * %s - 1.0f))", base); return;
    case 6: snprintf(buf, size, "(1.0f - %s)", base); return;
    case 7: snprintf(buf, size, "(2.0f * %s)", base); return;
    case 8: snprintf(buf, size, "(-2.0f * %s)", base); return;
    case 9: snprintf(buf, size, "(%s / %s.z)", base, base); return;     /* _dz (ps_1_4 texld/texcrd) */
    case 10: snprintf(buf, size, "(%s / %s.w)", base, base); return;    /* _dw */
    case 11: snprintf(buf, size, "abs(%s)", base); return;
    case 12: snprintf(buf, size, "(-abs(%s))", base); return;
    }
    fail(o, "source modifier %u", (p >> 24) & 0xF);
    snprintf(buf, size, "%s", base);
}

/* Destination register name for writes. */
static void dst_name(ctx *c, out *o, char *buf, size_t size, uint32_t p)
{
    int t = regtype(p), n = regnum(p);
    switch (t) {
    case RT_TEMP: snprintf(buf, size, "r%d", n); return;
    case RT_ADDR: snprintf(buf, size, c->vs ? "a0f" : "t%d", n); return;
    case RT_RASTOUT:
        if (n == 0) { snprintf(buf, size, "o.position"); return; }
        if (n == 1) { snprintf(buf, size, "fogv"); return; }
        if (n == 2) { snprintf(buf, size, "psizev"); return; }
        break;
    case RT_ATTROUT: snprintf(buf, size, "o.color%d", n); return;
    case RT_OUTPUT: snprintf(buf, size, "o.tex%d", n); return;
    case RT_COLOROUT: if (n == 0) { snprintf(buf, size, "oC0"); return; } break;
    case RT_DEPTHOUT: c->writes_depth = 1; snprintf(buf, size, "oDepth"); return;
    }
    fail(o, "destination register type %d number %d", t, n);
    snprintf(buf, size, "junk");
}

/* Write 'value' (a float4 expression) to the destination with mask, shift and saturate. */
static void write_dst(ctx *c, out *o, uint32_t p, const char *value)
{
    char d[64];
    dst_name(c, o, d, sizeof d, p);
    uint32_t mask = (p >> 16) & 0xF, mods = (p >> 20) & 0xF, shift = (p >> 24) & 0xF;
    char v[4096];
    snprintf(v, sizeof v, "%s", value);
    if (shift) {
        static const char *mul[16] = {0, "2.0f", "4.0f", "8.0f", 0, 0, 0, 0, 0, 0, 0, 0, 0, "0.125f", "0.25f", "0.5f"};
        if (!mul[shift]) fail(o, "destination shift %u", shift);
        char t[4200];
        snprintf(t, sizeof t, "((%s) * %s)", v, mul[shift] ? mul[shift] : "1.0f");
        snprintf(v, sizeof v, "%s", t);
    }
    if (mods & 1) {
        char t[4200];
        snprintf(t, sizeof t, "saturate(%s)", v);
        snprintf(v, sizeof v, "%s", t);
    } else if (c->ps1 && regtype(p) != RT_ADDR) {
        char t[4200];
        snprintf(t, sizeof t, "clamp(%s, -8.0f, 8.0f)", v);
        snprintf(v, sizeof v, "%s", t);
    }
    if (mods & 4) fail(o, "_centroid");
    int rt = regtype(p);
    if (rt == RT_ATTROUT) {                                         /* colour outputs are saturated */
        char t[4200];
        snprintf(t, sizeof t, "saturate(%s)", v);
        snprintf(v, sizeof v, "%s", t);
    }
    if (rt == RT_RASTOUT && regnum(p) != 0) {                       /* oFog, oPts are scalars */
        emit(o, "    %s = (%s).x;\n", d, v);
        return;
    }
    if (rt == RT_DEPTHOUT) { emit(o, "    %s = (%s).x;\n", d, v); return; }
    if (mask == 0xF) { emit(o, "    %s = %s;\n", d, v); return; }
    char m[5] = {0};
    int k = 0;
    for (int i = 0; i < 4; i++) if (mask & (1u << i)) m[k++] = comp[i];
    emit(o, "    %s.%s = (%s).%s;\n", d, m, v, m);
}

static const char *sampler_type(int dim) { return dim == 3 ? "texturecube<float>" : dim == 4 ? "texture3d<float>" : "texture2d<float>"; }

/* Sample stage s at coordinate expression (float4). */
static void sample(ctx *c, out *o, char *buf, size_t size, int s, const char *coord, int projected)
{
    if (s < 0 || s >= 16) { fail(o, "sampler %d", s); snprintf(buf, size, "float4(0)"); return; }
    c->sampler_used[s] = 1;
    int dim = c->sampler_dim[s] ? c->sampler_dim[s] : 2;
    char cc[1200];
    if (projected) snprintf(cc, sizeof cc, "((%s) / (%s).w)", coord, coord);
    else snprintf(cc, sizeof cc, "(%s)", coord);
    snprintf(buf, size, "HP_SAMPLE(tex%d, smp%d, %s.%s)", s, s, cc, dim == 2 ? "xy" : "xyz");
}

typedef struct { uint32_t tok, n; const uint32_t *p; } insn;

/* Scalar instructions read the replicated component; with no replicate swizzle
   (identity .xyzw) Direct3D uses .w. */
static const char *scalar(uint32_t p) { return ((p >> 16) & 0xFF) == 0xE4 ? "w" : "x"; }

/* One instruction. Returns 1 with *value and *dst set when it produces a register write
   (so co-issued pairs can read before writing); other effects are emitted directly. */
static int instruction(ctx *c, out *o, const insn *in, char *v, size_t vsize, uint32_t *dst)
{
    uint32_t op = in->tok & 0xFFFF;
    const uint32_t *p = in->p;
    char a[1600], b[1600], s2[1600];
    const uint32_t *rel[3] = {NULL, NULL, NULL};
    uint32_t si[3] = {0, 0, 0};
    int ns = 0;
    for (uint32_t k = 1; k < in->n && ns < 3; k++) {                /* sources; a 2.0 relative source carries an address token */
        si[ns] = k;
        if (c->major >= 2 && (p[k] & 0x2000) && k + 1 < in->n) { rel[ns] = &p[k + 1]; k++; }
        ns++;
    }
#define S0 (src(c, o, a, sizeof a, p[si[0]], rel[0]), a)
#define S1 (src(c, o, b, sizeof b, p[si[1]], rel[1]), b)
#define S2 (src(c, o, s2, sizeof s2, p[si[2]], rel[2]), s2)
    *dst = p[0];
    switch (op) {
    case 0: return 0;                                               /* nop */
    case 1:                                                         /* mov */
        if (c->vs && regtype(p[0]) == RT_ADDR) { emit(o, "    a0 = int4(floor(%s));\n", S0); return 0; }   /* vs_1_1: floor */
        snprintf(v, vsize, "%s", S0); return 1;
    case 46: emit(o, "    a0 = int4(rint(%s));\n", S0); return 0;   /* mova (2.0): round to nearest */
    case 2: snprintf(v, vsize, "(%s + %s)", S0, S1); return 1;
    case 3: snprintf(v, vsize, "(%s - %s)", S0, S1); return 1;
    case 4: snprintf(v, vsize, "(%s * %s + %s)", S0, S1, S2); return 1;
    case 5: snprintf(v, vsize, "(%s * %s)", S0, S1); return 1;
    case 6: snprintf(v, vsize, "float4(1.0f / (%s).%s)", S0, scalar(p[si[0]])); return 1;
    case 7: snprintf(v, vsize, "float4(rsqrt(abs((%s).%s)))", S0, scalar(p[si[0]])); return 1;
    case 8: snprintf(v, vsize, "float4(dot((%s).xyz, (%s).xyz))", S0, S1); return 1;
    case 9: snprintf(v, vsize, "float4(dot(%s, %s))", S0, S1); return 1;
    case 10: snprintf(v, vsize, "min(%s, %s)", S0, S1); return 1;
    case 11: snprintf(v, vsize, "max(%s, %s)", S0, S1); return 1;
    case 12: snprintf(v, vsize, "select(float4(0.0f), float4(1.0f), %s < %s)", S0, S1); return 1;
    case 13: snprintf(v, vsize, "select(float4(0.0f), float4(1.0f), %s >= %s)", S0, S1); return 1;
    case 14: snprintf(v, vsize, "float4(exp2((%s).%s))", S0, scalar(p[si[0]])); return 1;
    case 15: snprintf(v, vsize, "float4(log2(abs((%s).%s)))", S0, scalar(p[si[0]])); return 1;
    case 78: case 79: fail(o, "%s (partial-precision forms; not used by Halo)", op == 78 ? "expp" : "logp"); return 0;
    case 16: snprintf(v, vsize, "hp_lit(%s)", S0); return 1;
    case 17: snprintf(v, vsize, "hp_dst(%s, %s)", S0, S1); return 1;
    case 18: snprintf(v, vsize, "mix(%s, %s, %s)", S2, S1, S0); return 1;                 /* lrp: s0*(s1-s2)+s2 */
    case 19: snprintf(v, vsize, "fract(%s)", S0); return 1;
    case 32: snprintf(v, vsize, "float4(pow(abs((%s).%s), (%s).%s))", S0, scalar(p[si[0]]), S1, scalar(p[si[1]])); return 1;
    case 33: snprintf(v, vsize, "float4(cross((%s).xyz, (%s).xyz), 0.0f)", S0, S1); return 1;
    case 35: snprintf(v, vsize, "abs(%s)", S0); return 1;
    case 36: snprintf(v, vsize, "((%s) * rsqrt(dot((%s).xyz, (%s).xyz)))", S0, a, a); return 1;   /* nrm scales all four */
    case 37: snprintf(v, vsize, "hp_sincos((%s).%s)", S0, scalar(p[si[0]])); return 1;
    case 88: snprintf(v, vsize, "select(%s, %s, %s >= 0.0f)", S2, S1, S0); return 1;      /* cmp: s0 >= 0 ? s1 : s2 */
    case 90: snprintf(v, vsize, "float4(dot((%s).xy, (%s).xy) + (%s).x)", S0, S1, S2); return 1;   /* dp2add */
    case 80:                                                        /* cnd: 1.1-1.3 test r0.a only */
        if (c->ps1 && c->minor < 4) snprintf(v, vsize, "select(%s, %s, bool4(r0.w > 0.5f))", S2, S1);
        else snprintf(v, vsize, "select(%s, %s, %s > 0.5f)", S2, S1, S0);
        return 1;
    case 20: case 21: case 22: case 23: case 24: {                  /* m4x4, m4x3, m3x4, m3x3, m3x2 */
        int rows = op == 20 ? 4 : op == 21 ? 3 : op == 22 ? 4 : op == 23 ? 3 : 2;
        const char *sw = op >= 22 ? ".xyz" : "";
        char s0[1600];
        snprintf(s0, sizeof s0, "%s", S0);
        size_t k = (size_t)snprintf(v, vsize, "float4(");
        for (int r = 0; r < 4; r++) {
            if (r < rows) {
                src(c, o, b, sizeof b, p[si[1]] + (uint32_t)r, rel[1]);   /* consecutive registers */
                k += (size_t)snprintf(v + k, vsize - k, "%sdot((%s)%s, (%s)%s)", r ? ", " : "", s0, sw, b, sw);
            } else {
                k += (size_t)snprintf(v + k, vsize - k, ", 0.0f");
            }
        }
        snprintf(v + k, vsize - k, ")");
        return 1;
    }
    case 65: {                                                      /* texkill */
        int t = regnum(p[0]);
        if (regtype(p[0]) == RT_ADDR) emit(o, "    if (any(tc%d.xyz < 0.0f)) HP_KILL;\n", t);
        else emit(o, "    if (any(r%d.xyz < 0.0f)) HP_KILL;\n", t);
        return 0;
    }
    case 64:                                                        /* texcoord (1.1-1.3) / texcrd (1.4) */
        if (c->minor < 4) snprintf(v, vsize, "saturate(float4(tc%d.xyz, 1.0f))", regnum(p[0]));
        else snprintf(v, vsize, "%s", S0);
        return 1;
    case 66:                                                        /* tex (1.1-1.3), texld (1.4, 2.0) */
        if (c->ps1 && c->minor < 4) {
            char coord[16];
            int t = regnum(p[0]);
            snprintf(coord, sizeof coord, "tc%d", t);
            sample(c, o, v, vsize, t, coord, c->key && c->key->projected[t]);
        } else if (c->ps1) {
            sample(c, o, v, vsize, regnum(p[0]), S0, 0);           /* texld r<n>: stage n; t source carries _dz/_dw */
        } else {
            uint32_t control = (in->tok >> 16) & 0xFF;              /* 1: texldp, 2: texldb */
            if (control == 2) fail(o, "texldb");
            sample(c, o, v, vsize, regnum(p[si[1]]), S0, control == 1);
        }
        return 1;
    case 71: case 73:                                               /* texm3x2pad, texm3x3pad */
        emit(o, "    tm%d = dot(tc%d.xyz, (%s).xyz);\n", regnum(p[0]), regnum(p[0]), S0);
        return 0;
    case 72: {                                                      /* texm3x2tex */
        int t = regnum(p[0]);
        char coord[1800];
        snprintf(coord, sizeof coord, "float4(tm%d, dot(tc%d.xyz, (%s).xyz), 0.0f, 1.0f)", t - 1, t, S0);
        sample(c, o, v, vsize, t, coord, 0);
        return 1;
    }
    case 74: case 76: case 77: {                                    /* texm3x3tex, texm3x3spec, texm3x3vspec */
        int t = regnum(p[0]);
        char n[1800], coord[4000];
        snprintf(n, sizeof n, "float3(tm%d, tm%d, dot(tc%d.xyz, (%s).xyz))", t - 2, t - 1, t, S0);
        if (op == 74) snprintf(coord, sizeof coord, "float4(%s, 1.0f)", n);
        else if (op == 77) snprintf(coord, sizeof coord, "float4(hp_reflect(%s, float3(tc%d.w, tc%d.w, tc%d.w)), 1.0f)", n, t - 2, t - 1, t);
        else snprintf(coord, sizeof coord, "float4(hp_reflect(%s, (%s).xyz), 1.0f)", n, S1);
        sample(c, o, v, vsize, t, coord, 0);
        return 1;
    }
    case 0xFFFD: c->phase2 = 1; return 0;                           /* phase (1.4) */
    }
    fail(o, "opcode %u", op);
    return 0;
#undef S0
#undef S1
#undef S2
}

/* Decode the next instruction at tokens[i]; returns its token count (0 at END, -1 if malformed). */
static int decode(ctx *c, const uint32_t *t, uint32_t n, uint32_t i, insn *in)
{
    if (i >= n) return -1;
    uint32_t tok = t[i];
    if (tok == 0x0000FFFFu) return 0;
    uint32_t op = tok & 0xFFFF;
    if (op == 0xFFFE) { in->tok = tok; in->n = 0; in->p = t + i + 1; return (int)(1 + ((tok >> 16) & 0x7FFF)); }
    uint32_t k;
    if (c->major >= 2) k = (tok >> 24) & 0xF;
    else if (op == 81) k = 5;                                       /* def: register + 4 raw values */
    else { k = 0; while (i + 1 + k < n && (t[i + 1 + k] & 0x80000000u)) k++; }
    if (i + 1 + k > n) return -1;
    in->tok = tok; in->n = k; in->p = t + i + 1;
    return (int)(1 + k);
}

static const char *prelude =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct hp_vs_constants { float4 c[256]; int4 i[16]; uint b[16]; float4 fixup; };\n"
    "struct hp_ps_constants { float4 c[224]; int4 i[16]; uint b[16]; float4 fog_color; uint alpha_func; float alpha_ref; uint fog; uint pad; };\n"
    "struct hp_vs_out { float4 position [[position]]; float4 color0 [[user(c0)]]; float4 color1 [[user(c1)]];\n"
    "  float4 tex0 [[user(t0)]]; float4 tex1 [[user(t1)]]; float4 tex2 [[user(t2)]]; float4 tex3 [[user(t3)]];\n"
    "  float4 tex4 [[user(t4)]]; float4 tex5 [[user(t5)]]; float4 tex6 [[user(t6)]]; float4 tex7 [[user(t7)]];\n"
    "  float fog [[user(fog)]]; float psize [[point_size]]; };\n"
    "struct hp_ps_in { float4 position [[position]]; float4 color0 [[user(c0)]]; float4 color1 [[user(c1)]];\n"
    "  float4 tex0 [[user(t0)]]; float4 tex1 [[user(t1)]]; float4 tex2 [[user(t2)]]; float4 tex3 [[user(t3)]];\n"
    "  float4 tex4 [[user(t4)]]; float4 tex5 [[user(t5)]]; float4 tex6 [[user(t6)]]; float4 tex7 [[user(t7)]];\n"
    "  float fog [[user(fog)]]; };\n"
    "static inline float4 hp_c(constant hp_vs_constants &k, int i) { return (i >= 0 && i < 256) ? k.c[i] : float4(0.0f); }\n"
    "static inline float4 hp_lit(float4 s) {\n"
    "  float4 d = float4(1.0f, 0.0f, 0.0f, 1.0f);\n"
    "  if (s.x > 0.0f) { d.y = s.x; if (s.y > 0.0f) d.z = pow(s.y, clamp(s.w, -128.0f, 128.0f)); }\n"
    "  return d; }\n"
    "static inline float4 hp_dst(float4 a, float4 b) { return float4(1.0f, a.y * b.y, a.z, b.w); }\n"
    "static inline float4 hp_sincos(float x) { return float4(cos(x), sin(x), 0.0f, 0.0f); }\n"
    "static inline float3 hp_reflect(float3 n, float3 e) { return 2.0f * dot(n, e) / dot(n, n) * n - e; }\n"
    "static inline bool hp_alpha_pass(uint f, float a, float ref) {\n"
    "  float q = rint(saturate(a) * 255.0f);\n"
    "  switch (f) { case 1: return false; case 2: return q < ref; case 3: return q == ref; case 4: return q <= ref;\n"
    "    case 5: return q > ref; case 6: return q != ref; case 7: return q >= ref; default: return true; } }\n";

/* Translate one shader. Returns malloc'ed MSL source or NULL with err filled in. */
char *halopad_shader_to_msl(const uint32_t *t, uint32_t n, const hp_shader_key *key, char *err, size_t errlen)
{
    ctx c;
    memset(&c, 0, sizeof c);
    out o = {0};
    o.err = err; o.errlen = errlen;
    if (!n) { snprintf(err, errlen, "empty shader"); return NULL; }
    uint32_t ver = t[0];
    c.vs = ver >> 16 == 0xFFFE;
    if (!c.vs && ver >> 16 != 0xFFFF) { snprintf(err, errlen, "version token 0x%08x", ver); return NULL; }
    c.major = (int)((ver >> 8) & 0xFF); c.minor = (int)(ver & 0xFF);
    c.ps1 = !c.vs && c.major == 1;
    c.key = key;
    if (key) memcpy(c.sampler_dim, key->sampler_dim, 16);

    /* pass 1: declarations, definitions, sampler types */
    char defs[16384] = "";
    size_t dl = 0;
    char vs_inputs[4096] = "";
    size_t il = 0;
    insn in;
    int k;
    for (uint32_t i = 1; (k = decode(&c, t, n, i, &in)) > 0; i += (uint32_t)k) {
        uint32_t op = in.tok & 0xFFFF;
        if (op == 31 && in.n >= 2) {                                /* dcl */
            uint32_t r = in.p[1];
            if (regtype(r) == RT_SAMPLER) c.sampler_dim[regnum(r)] = (uint8_t)((in.p[0] >> 27) & 0xF);
            else if (c.vs && regtype(r) == RT_INPUT && !c.input_used[regnum(r)]) {
                c.input_used[regnum(r)] = 1;
                il += (size_t)snprintf(vs_inputs + il, sizeof vs_inputs - il, "  float4 v%d [[attribute(%d)]];\n", regnum(r), regnum(r));
            }
        } else if ((op == 74 || op == 76 || op == 77) && in.n >= 1 && !(key && key->sampler_dim[regnum(in.p[0])])) {
            c.sampler_dim[regnum(in.p[0])] = 3;                     /* texm3x3tex/spec/vspec sample a cube map */
        } else if (op == 81 && in.n >= 5) {                         /* def */
            int r = regnum(in.p[0]);
            c.def_f[r] = 1;
            dl += (size_t)snprintf(defs + dl, sizeof defs - dl,
                                   "    const float4 d%d = float4(as_type<float>(0x%08xu), as_type<float>(0x%08xu), as_type<float>(0x%08xu), as_type<float>(0x%08xu));\n",
                                   r, in.p[1], in.p[2], in.p[3], in.p[4]);
        }
    }
    if (k < 0) { snprintf(err, errlen, "malformed bytecode"); return NULL; }

    /* pass 2: body */
    out body = {0};
    body.err = err; body.errlen = errlen;
    uint32_t pending_dst = 0;
    char pending[4096];
    int have_pending = 0;
    for (uint32_t i = 1; (k = decode(&c, t, n, i, &in)) > 0; i += (uint32_t)k) {
        uint32_t op = in.tok & 0xFFFF;
        if (op == 0xFFFE || op == 31 || op == 81) continue;         /* comment, dcl, def */
        char v[4096];
        uint32_t d;
        int coissue = c.ps1 && (in.tok & 0x40000000u);
        int w = instruction(&c, &body, &in, v, sizeof v, &d);
        if (coissue && have_pending && w) {                         /* both read before either writes */
            emit(&body, "    { float4 hp_ci0 = %s;\n    float4 hp_ci1 = %s;\n", pending, v);
            write_dst(&c, &body, pending_dst, "hp_ci0");
            write_dst(&c, &body, d, "hp_ci1");
            emit(&body, "    }\n");
            have_pending = 0;
            continue;
        }
        if (have_pending) { write_dst(&c, &body, pending_dst, pending); have_pending = 0; }
        if (!w) continue;
        /* hold this write until we know whether the next instruction is co-issued with it */
        uint32_t next = i + (uint32_t)k;
        if (c.ps1 && next < n && (t[next] & 0x40000000u) && t[next] != 0x0000FFFFu) {
            snprintf(pending, sizeof pending, "%s", v);
            pending_dst = d;
            have_pending = 1;
        } else {
            write_dst(&c, &body, d, v);
        }
    }
    if (have_pending) write_dst(&c, &body, pending_dst, pending);
    if (k < 0) fail(&body, "malformed bytecode");
    if (body.failed) { free(body.s); free(o.s); return NULL; }

    emit(&o, "%s", prelude);
    int test = key && key->test_kernel;
    if (test) emit(&o, "#define HP_SAMPLE(t, s, c) t.sample(s, c, level(0))\n#define HP_KILL { outs[id * 2 + 1] = float4(1.0f); return; }\n");
    else emit(&o, "#define HP_SAMPLE(t, s, c) t.sample(s, c)\n#define HP_KILL discard_fragment()\n");
    if (c.vs) {
        emit(&o, "struct hp_vs_in {\n%s};\n", il ? vs_inputs : "  float4 v0 [[attribute(0)]];\n");
        if (test) {
            /* inputs: 16 float4 per case (v0-v15); outputs: position, colour 0-1, texture 0-7, (fog, psize) */
            emit(&o, "kernel void hp_vs_test(device const float4 *ins [[buffer(0)]], device float4 *outs [[buffer(1)]],\n"
                     "    constant hp_vs_constants &k [[buffer(16)]], uint id [[thread_position_in_grid]]) {\n    hp_vs_in in;\n");
            for (int r = 0; r < 16; r++) if (c.input_used[r]) emit(&o, "    in.v%d = ins[id * 16 + %d];\n", r, r);
        } else {
            emit(&o, "vertex hp_vs_out hp_vs(hp_vs_in in [[stage_in]], constant hp_vs_constants &k [[buffer(16)]]) {\n");
        }
        emit(&o, "    hp_vs_out o = {};\n    int4 a0 = int4(0);\n    float fogv = 1.0f, psizev = 1.0f;\n");
        for (int r = 0; r < 32; r++) emit(&o, "    float4 r%d = float4(0.0f);\n", r);
        emit(&o, "%s%s", defs, body.s ? body.s : "");
        emit(&o, "    o.fog = fogv; o.psize = psizev;\n");
        emit(&o, "    o.position.xy += k.fixup.xy * o.position.w;   /* Direct3D 9 pixel centres */\n");
        emit(&o, "    o.position.z += k.fixup.z * o.position.w;     /* DEPTHBIAS (window-depth units) */\n");
        if (test) {
            emit(&o, "    device float4 *w = outs + id * 12;\n    w[0] = o.position; w[1] = o.color0; w[2] = o.color1;\n");
            for (int s = 0; s < 8; s++) emit(&o, "    w[%d] = o.tex%d;\n", 3 + s, s);
            emit(&o, "    w[11] = float4(o.fog, o.psize, 0.0f, 0.0f);\n}\n");
        } else {
            emit(&o, "    return o;\n}\n");
        }
    } else {
        if (test)
            /* inputs: colour 0-1, texture 0-7, (fog) per case; outputs: colour, (killed) */
            emit(&o, "kernel void hp_ps_test(device const float4 *ins [[buffer(1)]], device float4 *outs [[buffer(2)]],\n"
                     "    constant hp_ps_constants &k [[buffer(0)]], uint id [[thread_position_in_grid]]");
        else
            emit(&o, "fragment float4 hp_ps(hp_ps_in in [[stage_in]], constant hp_ps_constants &k [[buffer(0)]]");
        for (int s = 0; s < 16; s++)
            if (c.sampler_used[s])
                emit(&o, ",\n    %s tex%d [[texture(%d)]], sampler smp%d [[sampler(%d)]]", sampler_type(c.sampler_dim[s] ? c.sampler_dim[s] : 2), s, s, s, s);
        emit(&o, ") {\n");
        if (test) {
            emit(&o, "    hp_ps_in in;\n    in.color0 = ins[id * 11]; in.color1 = ins[id * 11 + 1];\n");
            for (int s = 0; s < 8; s++) emit(&o, "    in.tex%d = ins[id * 11 + %d];\n", s, 2 + s);
            emit(&o, "    in.fog = ins[id * 11 + 10].x;\n    outs[id * 2 + 1] = float4(0.0f);\n");
        }
        for (int s = 0; s < 8; s++) emit(&o, "    float4 tc%d = in.tex%d;\n", s, s);
        emit(&o, "    float4 v0 = saturate(in.color0), v1 = saturate(in.color1);\n    float4 oC0 = float4(0.0f);\n");
        for (int r = 0; r < 12; r++) emit(&o, "    float4 r%d = float4(0.0f);\n", r);
        for (int s = 0; s < 8; s++) emit(&o, "    float4 t%d = float4(0.0f); float tm%d = 0.0f;\n", s, s);
        emit(&o, "%s%s", defs, body.s ? body.s : "");
        emit(&o, "    float4 color = %s;\n", c.ps1 ? "saturate(r0)" : "oC0");
        emit(&o, "    if (!hp_alpha_pass(k.alpha_func, color.w, k.alpha_ref)) HP_KILL;\n");
        emit(&o, "    if (k.fog) color.xyz = mix(k.fog_color.xyz, color.xyz, saturate(in.fog));\n");
        if (test) emit(&o, "    outs[id * 2] = color;\n}\n");
        else emit(&o, "    return color;\n}\n");
        if (c.writes_depth) { fail(&o, "oDepth output"); }
    }
    free(body.s);
    if (o.failed) { free(o.s); return NULL; }
    return o.s;
}

/* Vertex shader inputs: for each register v<n> it declares, the Direct3D usage and usage
   index (dcl_<usage><index> v<n>). Returns the number of declared inputs, or -1. */
int halopad_shader_vs_inputs(const uint32_t *t, uint32_t n, uint8_t usage[16], uint8_t index[16], uint8_t used[16])
{
    ctx c;
    memset(&c, 0, sizeof c);
    if (!n || t[0] >> 16 != 0xFFFE) return -1;
    c.vs = 1; c.major = (int)((t[0] >> 8) & 0xFF); c.minor = (int)(t[0] & 0xFF);
    memset(used, 0, 16);
    int count = 0, k;
    insn in;
    for (uint32_t i = 1; (k = decode(&c, t, n, i, &in)) > 0; i += (uint32_t)k) {
        if ((in.tok & 0xFFFF) != 31 || in.n < 2 || regtype(in.p[1]) != RT_INPUT) continue;
        int r = regnum(in.p[1]);
        if (r >= 16) return -1;
        usage[r] = (uint8_t)(in.p[0] & 0x1F);
        index[r] = (uint8_t)((in.p[0] >> 16) & 0xF);
        if (!used[r]) count++;
        used[r] = 1;
    }
    return k < 0 ? -1 : count;
}
