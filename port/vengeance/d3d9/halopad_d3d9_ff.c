/* HaloPad Direct3D 9, part 5 (G4): fixed-function vertex and pixel processing as generated
 * Metal Shading Language.
 *
 * A key built from the device state selects the program; its constants come from the
 * current transforms, material, lights and render states. Direct3D 9 definitions:
 *   vertex: pretransformed (POSITIONT / XYZRHW) positions map back to clip space with z kept
 *     as given; otherwise world*view*projection. Lighting: global ambient plus, per enabled
 *     light, ambient + diffuse (N.L) + specular (Blinn half vector, only where N.L > 0, local
 *     or infinite viewer), with range, 1/(a0 + a1 d + a2 d^2) attenuation and spot falloff;
 *     colour sources follow COLORVERTEX and the *MATERIALSOURCE states; unlit vertices pass
 *     their colours (diffuse defaults to white, specular to black). Texture coordinates per
 *     stage: the TEXCOORDINDEX set or camera-space normal/position/reflection, then the
 *     texture transform with 1-3 component inputs padded as (u,1,0,0)/(u,v,1,0)/(u,v,w,1).
 *     Vertex fog: linear/exp/exp2 on view depth or range; pretransformed vertices take fog
 *     from specular alpha.
 *   pixel: the texture stage cascade (all D3DTOP operations except bump mapping), arguments
 *     with complement and alpha replicate (a stage with no texture reads white, which is
 *     why the default MODULATE(TEXTURE, DIFFUSE) shows vertex colours), the temp register,
 *     each stage clamped to [0, 1],
 *     specular added when SPECULARENABLE, then fog and alpha test.
 * Anything outside that stops with a message naming it. */
#include "halopad_win32.h"
#include "halopad_d3d9_internal.h"
#include <math.h>
#include <stdarg.h>

typedef struct {
    uint8_t pretransformed, has_normal, has_color[2], has_psize, tex_size[8];
    uint8_t lighting, normalize, local_viewer, light_type[8];
    uint8_t src_diffuse, src_specular, src_ambient, src_emissive;   /* 0 material, 1 vertex diffuse, 2 vertex specular */
    uint8_t fog_mode, range_fog;
    uint8_t stage_index[8], stage_gen[8], stage_count[8], stage_proj[8];
} hp_ff_vs_key;

typedef struct {
    struct { uint8_t cop, carg[3], aop, aarg[3], result, tex_dim, projected; } st[8];
    uint8_t specular;
} hp_ff_ps_key;

typedef struct { char *s; size_t n, cap; } sb;
static void add(sb *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char tmp[2048];
    int k = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (b->n + (size_t)k + 1 > b->cap) { b->cap = (b->cap + (size_t)k + 1) * 2; b->s = realloc(b->s, b->cap); }
    memcpy(b->s + b->n, tmp, (size_t)k + 1);
    b->n += (size_t)k;
}

static const char *common =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct hp_vs_out { float4 position [[position]]; float4 color0 [[user(c0)]]; float4 color1 [[user(c1)]];\n"
    "  float4 tex0 [[user(t0)]]; float4 tex1 [[user(t1)]]; float4 tex2 [[user(t2)]]; float4 tex3 [[user(t3)]];\n"
    "  float4 tex4 [[user(t4)]]; float4 tex5 [[user(t5)]]; float4 tex6 [[user(t6)]]; float4 tex7 [[user(t7)]];\n"
    "  float fog [[user(fog)]]; float psize [[point_size]]; };\n"
    "struct hp_ps_in { float4 position [[position]]; float4 color0 [[user(c0)]]; float4 color1 [[user(c1)]];\n"
    "  float4 tex0 [[user(t0)]]; float4 tex1 [[user(t1)]]; float4 tex2 [[user(t2)]]; float4 tex3 [[user(t3)]];\n"
    "  float4 tex4 [[user(t4)]]; float4 tex5 [[user(t5)]]; float4 tex6 [[user(t6)]]; float4 tex7 [[user(t7)]];\n"
    "  float fog [[user(fog)]]; };\n"
    "struct hp_light { float4 diffuse, specular, ambient, position, direction, atten, spot; };\n"
    "struct hp_ff_vs_constants { float4x4 mvp, mv, normal; float4x4 tex[8]; float4 fixup, viewport, depth, fog, ambient;\n"
    "  float4 mat_diffuse, mat_ambient, mat_specular, mat_emissive, mat_power; hp_light light[8]; };\n"
    "struct hp_ff_ps_constants { float4 tfactor; float4 fog_color; uint alpha_func; float alpha_ref; uint fog; uint pad; float4 stage_const[8]; };\n"
    "static inline bool hp_alpha_pass(uint f, float a, float ref) {\n"
    "  float q = rint(saturate(a) * 255.0f);\n"
    "  switch (f) { case 1: return false; case 2: return q < ref; case 3: return q == ref; case 4: return q <= ref;\n"
    "    case 5: return q > ref; case 6: return q != ref; case 7: return q >= ref; default: return true; } }\n";

char *halopad_ff_vs_msl(const hp_ff_vs_key *k)
{
    sb b = {0};
    add(&b, "%s", common);
    add(&b, "struct hp_vs_in {\n  float4 v0 [[attribute(0)]];\n");
    if (k->has_normal) add(&b, "  float4 v1 [[attribute(1)]];\n");
    for (int c = 0; c < 2; c++) if (k->has_color[c]) add(&b, "  float4 v%d [[attribute(%d)]];\n", 2 + c, 2 + c);
    if (k->has_psize) add(&b, "  float4 v4 [[attribute(4)]];\n");
    for (int t = 0; t < 8; t++) if (k->tex_size[t]) add(&b, "  float4 v%d [[attribute(%d)]];\n", 5 + t, 5 + t);
    add(&b, "};\nvertex hp_vs_out hp_vs(hp_vs_in in [[stage_in]], constant hp_ff_vs_constants &k [[buffer(16)]]) {\n    hp_vs_out o = {};\n");
    if (k->pretransformed) {
        add(&b, "    float w = 1.0f / in.v0.w;\n"
                "    float2 ndc = float2((in.v0.x - k.viewport.x) / k.viewport.z * 2.0f - 1.0f, 1.0f - (in.v0.y - k.viewport.y) / k.viewport.w * 2.0f);\n"
                "    float z = k.depth.y != 0.0f ? (in.v0.z - k.depth.x) / k.depth.y : in.v0.z;\n"
                "    o.position = float4(ndc * w, z * w, w);\n    float4 V = float4(0.0f, 0.0f, 0.0f, 1.0f);\n");
    } else {
        add(&b, "    float4 P = float4(in.v0.xyz, 1.0f);\n    o.position = k.mvp * P;\n    float4 V = k.mv * P;\n");
    }
    add(&b, "    float3 N = %s;\n", k->has_normal ? "(k.normal * float4(in.v1.xyz, 0.0f)).xyz" : "float3(0.0f)");
    if (k->normalize && k->has_normal) add(&b, "    N = normalize(N);\n");
    add(&b, "    float4 vdiff = %s, vspec = %s;\n", k->has_color[0] ? "in.v2" : "float4(1.0f)", k->has_color[1] ? "in.v3" : "float4(0.0f)");
    if (k->lighting && !k->pretransformed) {
        static const char *srcs[3] = {0, "vdiff", "vspec"};
        char dm[32], am[32], sm[32], em[32];
        snprintf(dm, sizeof dm, "%s", k->src_diffuse ? srcs[k->src_diffuse] : "k.mat_diffuse");
        snprintf(am, sizeof am, "%s", k->src_ambient ? srcs[k->src_ambient] : "k.mat_ambient");
        snprintf(sm, sizeof sm, "%s", k->src_specular ? srcs[k->src_specular] : "k.mat_specular");
        snprintf(em, sizeof em, "%s", k->src_emissive ? srcs[k->src_emissive] : "k.mat_emissive");
        add(&b, "    float3 amb = k.ambient.xyz, dif = float3(0.0f), spe = float3(0.0f);\n");
        add(&b, "    float3 E = %s;\n", k->local_viewer ? "normalize(-V.xyz)" : "float3(0.0f, 0.0f, -1.0f)");
        for (int l = 0; l < 8; l++) {
            if (!k->light_type[l]) continue;
            add(&b, "    {\n        hp_light L = k.light[%d];\n", l);
            if (k->light_type[l] == 3) {
                add(&b, "        float3 Ld = normalize(-L.direction.xyz);\n        float att = 1.0f;\n");
            } else {
                add(&b, "        float3 Lv = L.position.xyz - V.xyz;\n        float d = length(Lv);\n        float3 Ld = Lv / max(d, 1e-20f);\n"
                        "        float att = d > L.atten.x ? 0.0f : 1.0f / (L.atten.z + L.atten.w * d + L.spot.x * d * d);\n");
                if (k->light_type[l] == 2)
                    add(&b, "        float rho = dot(-Ld, normalize(L.direction.xyz));\n"
                            "        att *= rho > L.spot.y ? 1.0f : rho <= L.spot.z ? 0.0f : pow(max((rho - L.spot.z) / (L.spot.y - L.spot.z), 0.0f), L.atten.y);\n");
            }
            add(&b, "        float nl = dot(N, Ld);\n        amb += L.ambient.xyz * att;\n"
                    "        if (nl > 0.0f) {\n            dif += L.diffuse.xyz * nl * att;\n"
                    "            float3 H = normalize(Ld - E);\n"
                    "            spe += L.specular.xyz * pow(max(dot(N, H), 0.0f), k.mat_power.x) * att;\n        }\n    }\n");
        }
        add(&b, "    o.color0 = saturate(float4(%s.xyz + %s.xyz * amb + %s.xyz * dif, %s.w));\n", em, am, dm, dm);
        add(&b, "    o.color1 = saturate(float4(%s.xyz * spe, 0.0f));\n", sm);
    } else {
        add(&b, "    o.color0 = saturate(vdiff);\n    o.color1 = saturate(vspec);\n");
    }
    for (int s = 0; s < 8; s++) {
        uint8_t set = k->stage_index[s], gen = k->stage_gen[s], count = k->stage_count[s];
        char in[96];
        int size = 4;
        if (gen == 0) {
            if (set >= 8 || !k->tex_size[set]) { add(&b, "    o.tex%d = float4(0.0f, 0.0f, 0.0f, 1.0f);\n", s); continue; }
            snprintf(in, sizeof in, "in.v%d", 5 + set);
            size = k->tex_size[set];
        } else if (gen == 1) { snprintf(in, sizeof in, "float4(N, 1.0f)"); size = 3; }
        else if (gen == 2) { snprintf(in, sizeof in, "float4(V.xyz, 1.0f)"); size = 3; }
        else { snprintf(in, sizeof in, "float4(reflect(normalize(V.xyz), N), 1.0f)"); size = 3; }
        if (!count) { add(&b, "    o.tex%d = %s;\n", s, in); continue; }
        static const char *pad[5] = {0, "float4(%s.x, 1.0f, 0.0f, 0.0f)", "float4(%s.xy, 1.0f, 0.0f)", "float4(%s.xyz, 1.0f)", "%s"};
        char p[160];
        snprintf(p, sizeof p, pad[size], in);
        add(&b, "    o.tex%d = k.tex[%d] * %s;\n", s, s, p);
        if (k->stage_proj[s] && count < 4) add(&b, "    o.tex%d.w = o.tex%d[%d];\n", s, s, count - 1);
    }
    if (k->pretransformed) add(&b, "    o.fog = %s;\n", k->has_color[1] ? "in.v3.w" : "1.0f");
    else if (k->fog_mode) {
        add(&b, "    float fd = %s;\n", k->range_fog ? "length(V.xyz)" : "abs(V.z)");
        if (k->fog_mode == 3) add(&b, "    o.fog = saturate((k.fog.y - fd) / (k.fog.y - k.fog.x));\n");
        else if (k->fog_mode == 1) add(&b, "    o.fog = saturate(exp(-k.fog.z * fd));\n");
        else add(&b, "    o.fog = saturate(exp(-(k.fog.z * fd) * (k.fog.z * fd)));\n");
    } else {
        add(&b, "    o.fog = 1.0f;\n");
    }
    add(&b, "    o.psize = %s;\n", k->has_psize ? "in.v4.x" : "k.fog.w");
    add(&b, "    o.position.xy += k.fixup.xy * o.position.w;\n    o.position.z += k.fixup.z * o.position.w;\n    return o;\n}\n");
    return b.s;
}

static void arg(sb *b, uint8_t a, int stage, const char *out, int *fail)
{
    const char *base;
    switch (a & 7) {
    case 0: base = "diffuse"; break;
    case 1: base = "current"; break;
    case 2: base = "texv"; break;
    case 3: base = "k.tfactor"; break;
    case 4: base = "specular"; break;
    case 5: base = "temp"; break;
    case 6: base = NULL; break;
    default: *fail = 1; return;
    }
    char v[64];
    if (base) snprintf(v, sizeof v, "%s", base);
    else snprintf(v, sizeof v, "k.stage_const[%d]", stage);
    if (a & 0x20) { char t[80]; snprintf(t, sizeof t, "%s.wwww", v); snprintf(v, sizeof v, "%s", t); }   /* ALPHAREPLICATE */
    if (a & 0x10) add(b, "        float4 %s = 1.0f - %s;\n", out, v);                                   /* COMPLEMENT */
    else add(b, "        float4 %s = %s;\n", out, v);
    if (a & ~0x37u) *fail = 1;
}

/* One D3DTOP on arguments a0, a1, a2 (names), for channel selector ch ("xyz" or "w"). */
static int op(sb *b, uint8_t o, const char *dst, const char *ch)
{
    const char *f;
    switch (o) {
    case 2: f = "a1"; break;
    case 3: f = "a2"; break;
    case 4: f = "a1 * a2"; break;
    case 5: f = "2.0f * a1 * a2"; break;
    case 6: f = "4.0f * a1 * a2"; break;
    case 7: f = "a1 + a2"; break;
    case 8: f = "a1 + a2 - 0.5f"; break;
    case 9: f = "2.0f * (a1 + a2 - 0.5f)"; break;
    case 10: f = "a1 - a2"; break;
    case 11: f = "a1 + a2 - a1 * a2"; break;
    case 12: f = "a1 * diffuse.w + a2 * (1.0f - diffuse.w)"; break;
    case 13: f = "a1 * texv.w + a2 * (1.0f - texv.w)"; break;
    case 14: f = "a1 * k.tfactor.w + a2 * (1.0f - k.tfactor.w)"; break;
    case 15: f = "a1 + a2 * (1.0f - texv.w)"; break;
    case 16: f = "a1 * current.w + a2 * (1.0f - current.w)"; break;
    case 18: f = "a1 + a1.wwww * a2"; break;                        /* MODULATEALPHA_ADDCOLOR */
    case 19: f = "a1 * a2 + a1.wwww"; break;                        /* MODULATECOLOR_ADDALPHA */
    case 20: f = "(1.0f - a1.wwww) * a2 + a1"; break;               /* MODULATEINVALPHA_ADDCOLOR */
    case 21: f = "(1.0f - a1) * a2 + a1.wwww"; break;               /* MODULATEINVCOLOR_ADDALPHA */
    case 24: f = "float4(saturate(4.0f * dot(a1.xyz - 0.5f, a2.xyz - 0.5f)))"; break;   /* DOTPRODUCT3 */
    case 25: f = "a0 + a1 * a2"; break;                             /* MULTIPLYADD */
    case 26: f = "a0 * a1 + (1.0f - a0) * a2"; break;               /* LERP */
    default: return 0;
    }
    add(b, "        %s.%s = (%s).%s;\n", dst, ch, f, ch);
    return 1;
}

char *halopad_ff_ps_msl(const hp_ff_ps_key *k, char *err, size_t errlen)
{
    sb b = {0};
    add(&b, "%s", common);
    add(&b, "fragment float4 hp_ps(hp_ps_in in [[stage_in]], constant hp_ff_ps_constants &k [[buffer(0)]]");
    int last = 0;
    while (last < 8 && k->st[last].cop != 1) last++;
    for (int s = 0; s < last; s++) {
        if (!k->st[s].tex_dim) continue;
        const char *type = k->st[s].tex_dim == 3 ? "texturecube<float>" : k->st[s].tex_dim == 4 ? "texture3d<float>" : "texture2d<float>";
        add(&b, ",\n    %s tex%d [[texture(%d)]], sampler smp%d [[sampler(%d)]]", type, s, s, s, s);
    }
    add(&b, ") {\n    float4 diffuse = saturate(in.color0), specular = saturate(in.color1);\n"
            "    float4 current = diffuse, temp = float4(0.0f);\n");
    for (int s = 0; s < last; s++) {
        int fail = 0;
        const typeof(k->st[0]) *t = &k->st[s];
        int uses_tex = 0;
        for (int i = 0; i < 3; i++) uses_tex |= (t->carg[i] & 7) == 2 || (t->aarg[i] & 7) == 2;
        uses_tex |= t->cop == 13 || t->cop == 15 || t->aop == 13 || t->aop == 15;
        if (t->cop == 22 || t->cop == 23 || t->aop == 22 || t->aop == 23) { snprintf(err, errlen, "stage %d bump mapping", s); free(b.s); return NULL; }
        add(&b, "    {\n        float4 texv = float4(1.0f);\n");
        if (uses_tex && t->tex_dim) {                               /* no texture bound: reads opaque white */
            const char *sw = t->tex_dim == 2 ? "xy" : "xyz";
            if (t->projected) add(&b, "        texv = tex%d.sample(smp%d, (in.tex%d.%s / in.tex%d.w));\n", s, s, s, sw, s);
            else add(&b, "        texv = tex%d.sample(smp%d, in.tex%d.%s);\n", s, s, s, sw);
        }
        add(&b, "        float4 r = current;\n");
        const uint8_t cargs[3] = {t->carg[0], t->carg[1], t->carg[2]}, aargs[3] = {t->aarg[0], t->aarg[1], t->aarg[2]};
        add(&b, "        {\n");
        arg(&b, cargs[0], s, "a0", &fail); arg(&b, cargs[1], s, "a1", &fail); arg(&b, cargs[2], s, "a2", &fail);
        if (!op(&b, t->cop, "r", t->cop == 24 ? "xyzw" : "xyz")) fail = 2;
        add(&b, "        }\n");
        if (t->cop != 24) {                                         /* DOTPRODUCT3 fills alpha too */
            if (t->aop == 1) {
                add(&b, "        r.w = current.w;\n");
            } else {
                add(&b, "        {\n");
                arg(&b, aargs[0], s, "a0", &fail); arg(&b, aargs[1], s, "a1", &fail); arg(&b, aargs[2], s, "a2", &fail);
                if (!op(&b, t->aop, "r", "w")) fail = 3;
                add(&b, "        }\n");
            }
        }
        add(&b, "        %s = saturate(r);\n    }\n", t->result == 5 ? "temp" : "current");
        if (fail) {
            snprintf(err, errlen, "stage %d: colour op %u / alpha op %u / arguments %x %x %x, %x %x %x", s, t->cop, t->aop,
                     cargs[0], cargs[1], cargs[2], aargs[0], aargs[1], aargs[2]);
            free(b.s);
            return NULL;
        }
    }
    add(&b, "    float4 color = current;\n");
    if (k->specular) add(&b, "    color.xyz = saturate(color.xyz + specular.xyz);\n");
    add(&b, "    if (!hp_alpha_pass(k.alpha_func, color.w, k.alpha_ref)) discard_fragment();\n"
            "    if (k.fog) color.xyz = mix(k.fog_color.xyz, color.xyz, saturate(in.fog));\n    return color;\n}\n");
    return b.s;
}

/* ---- keys and constants from device state ---- */

static void mul(const float *a, const float *b, float *c)   /* row-major 4x4, c = a * b */
{
    float t[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            t[4 * i + j] = a[4 * i] * b[j] + a[4 * i + 1] * b[4 + j] + a[4 * i + 2] * b[8 + j] + a[4 * i + 3] * b[12 + j];
    memcpy(c, t, sizeof t);
}

static void normal_matrix(const float *m, float *n)          /* inverse-transpose of the upper 3x3, as a 4x4 */
{
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    float r = det != 0.0f ? 1.0f / det : 0.0f;
    float inv[9] = {(e * i - f * h) * r, (c * h - b * i) * r, (b * f - c * e) * r,
                    (f * g - d * i) * r, (a * i - c * g) * r, (c * d - a * f) * r,
                    (d * h - e * g) * r, (b * g - a * h) * r, (a * e - b * d) * r};
    memset(n, 0, 64);
    for (int x = 0; x < 3; x++) for (int y = 0; y < 3; y++) n[4 * x + y] = inv[3 * y + x];
    n[15] = 1.0f;
}

static float fbits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static void color4(uint32_t c, float *out)
{
    out[0] = ((c >> 16) & 0xFF) / 255.0f; out[1] = ((c >> 8) & 0xFF) / 255.0f; out[2] = (c & 0xFF) / 255.0f; out[3] = (c >> 24) / 255.0f;
}

/* elements: the draw's (usage, index, components) triples. Fills the vertex key and the
   FF vertex shader's input table (register <- usage/index) for the draw's layout. */
void halopad_ff_vs_key(device *d, const uint8_t (*el)[3], int ne, hp_ff_vs_key *k, uint8_t in_usage[16], uint8_t in_index[16],
                       uint8_t in_used[16])
{
    memset(k, 0, sizeof *k);
    memset(in_used, 0, 16);
    for (int i = 0; i < ne; i++) {
        uint8_t u = el[i][0], x = el[i][1], n = el[i][2];
        int reg = -1;
        if ((u == 0 || u == 9) && x == 0) { reg = 0; k->pretransformed = u == 9; }
        else if (u == 3 && x == 0) { reg = 1; k->has_normal = 1; }
        else if (u == 10 && x < 2) { reg = 2 + x; k->has_color[x] = 1; }
        else if (u == 4 && x == 0) { reg = 4; k->has_psize = 1; }
        else if (u == 5 && x < 8) { reg = 5 + x; k->tex_size[x] = n; }
        if (reg < 0) continue;
        in_usage[reg] = u; in_index[reg] = x; in_used[reg] = 1;
    }
    if (!in_used[0]) hp_unsupported("draw", "fixed-function vertices without a position");
    const uint32_t *rs = d->rs;
    k->lighting = rs[137] != 0;
    k->normalize = rs[143] != 0;
    k->local_viewer = rs[142] != 0;
    if (rs[151] || rs[167]) hp_unsupported("draw", "fixed-function vertex blending");
    int nl = 0;
    for (uint32_t i = 0; i < d->nlight && nl < 8; i++) {
        if (!d->light[i].enabled) continue;
        uint32_t type;
        memcpy(&type, &d->light[i].light[0], 4);
        k->light_type[nl++] = (uint8_t)(type == 1 ? 1 : type == 2 ? 2 : 3);
    }
    /* colour sources: D3DMCS_MATERIAL 0, COLOR1 1, COLOR2 2; vertex colours only with COLORVERTEX */
    uint32_t src[4] = {rs[145], rs[146], rs[147], rs[148]};
    uint8_t *dst[4] = {&k->src_diffuse, &k->src_specular, &k->src_ambient, &k->src_emissive};
    for (int i = 0; i < 4; i++) {
        uint32_t s = rs[141] ? src[i] : 0;
        if (s > 2) hp_unsupported("draw", "material source %u", s);
        *dst[i] = (uint8_t)((s == 1 && k->has_color[0]) || (s == 2 && k->has_color[1]) ? s : 0);
    }
    if (rs[28] && !rs[35]) { k->fog_mode = (uint8_t)rs[140]; k->range_fog = rs[48] != 0; }
    if (k->fog_mode > 3) hp_unsupported("draw", "fog vertex mode %u", k->fog_mode);
    for (int s = 0; s < 8; s++) {
        uint32_t tci = d->tss[s][11], ttf = d->tss[s][24];
        k->stage_index[s] = (uint8_t)(tci & 0xFFFF);
        uint32_t gen = tci >> 16;
        if (gen > 3) hp_unsupported("draw", "texture coordinate generation mode %u (sphere map)", gen);
        k->stage_gen[s] = (uint8_t)gen;
        k->stage_count[s] = (uint8_t)(ttf & 0xFF);
        k->stage_proj[s] = (ttf & 0x100) != 0;
        if (k->stage_count[s] > 4) hp_unsupported("draw", "texture transform count %u", k->stage_count[s]);
    }
}

void halopad_ff_vs_constants(device *d, uint8_t out[1760], const float fixup[4])
{
    float *f = (float *)out;
    memset(out, 0, 1760);                                         /* 440 floats: see hp_ff_vs_constants */
    const float *world = d->transform[256], *view = d->transform[2], *proj = d->transform[3];
    float mv[16], mvp[16];
    mul(world, view, mv);
    mul(mv, proj, mvp);
    memcpy(f, mvp, 64); memcpy(f + 16, mv, 64); normal_matrix(mv, f + 32);
    for (int s = 0; s < 8; s++) memcpy(f + 48 + 16 * s, d->transform[16 + s], 64);
    float *tail = f + 48 + 128;                                     /* fixup, viewport, depth, fog, ambient */
    memcpy(tail, fixup, 16);
    tail[4] = (float)d->viewport[0]; tail[5] = (float)d->viewport[1]; tail[6] = (float)d->viewport[2]; tail[7] = (float)d->viewport[3];
    float minz = fbits(d->viewport[4]), maxz = fbits(d->viewport[5]);
    tail[8] = minz; tail[9] = maxz - minz;
    tail[12] = fbits(d->rs[36]); tail[13] = fbits(d->rs[37]); tail[14] = fbits(d->rs[38]); tail[15] = fbits(d->rs[154]);
    color4(d->rs[139], tail + 16);
    float *mat = tail + 20;                                         /* diffuse, ambient, specular, emissive, power */
    memcpy(mat, d->material, 64);
    mat[16] = d->material[16];
    float *lt = mat + 20;
    int nl = 0;
    for (uint32_t i = 0; i < d->nlight && nl < 8; i++) {
        if (!d->light[i].enabled) continue;
        const float *L = d->light[i].light;
        float *o = lt + 28 * nl++;
        memcpy(o, L + 1, 16); memcpy(o + 4, L + 5, 16); memcpy(o + 8, L + 9, 16);   /* diffuse, specular, ambient */
        float p[4] = {L[13], L[14], L[15], 1.0f}, dv[4] = {L[16], L[17], L[18], 0.0f};
        for (int j = 0; j < 4; j++) {                               /* to view space (row vector * view) */
            o[12 + j] = p[0] * view[j] + p[1] * view[4 + j] + p[2] * view[8 + j] + p[3] * view[12 + j];
            o[16 + j] = dv[0] * view[j] + dv[1] * view[4 + j] + dv[2] * view[8 + j];
        }
        o[20] = L[19]; o[21] = L[20]; o[22] = L[21]; o[23] = L[22];  /* range, falloff, att0, att1 */
        o[24] = L[23]; o[25] = cosf(L[24] * 0.5f); o[26] = cosf(L[25] * 0.5f);   /* att2, cos(theta/2), cos(phi/2) */
    }
}

void halopad_ff_ps_key(device *d, const uint8_t tex_dim[16], hp_ff_ps_key *k)
{
    memset(k, 0, sizeof *k);
    for (int s = 0; s < 8; s++) {
        const uint32_t *t = d->tss[s];
        k->st[s].cop = (uint8_t)t[1]; k->st[s].carg[0] = (uint8_t)t[26]; k->st[s].carg[1] = (uint8_t)t[2]; k->st[s].carg[2] = (uint8_t)t[3];
        k->st[s].aop = (uint8_t)t[4]; k->st[s].aarg[0] = (uint8_t)t[27]; k->st[s].aarg[1] = (uint8_t)t[5]; k->st[s].aarg[2] = (uint8_t)t[6];
        k->st[s].result = (uint8_t)t[28];
        k->st[s].tex_dim = tex_dim[s];
        k->st[s].projected = (t[24] & 0x100) != 0;
        if (t[28] != 1 && t[28] != 5) hp_unsupported("draw", "stage %d result argument %u", s, t[28]);
    }
    k->specular = d->rs[29] != 0;
}

void halopad_ff_ps_constants(device *d, uint8_t out[176])
{
    float *f = (float *)out;
    memset(out, 0, 176);
    color4(d->rs[60], f);                                           /* TEXTUREFACTOR */
    color4(d->rs[34], f + 4);                                       /* FOGCOLOR */
    uint32_t alpha_func = d->rs[15] ? d->rs[25] : 8u, fog = d->rs[28] ? 1u : 0u;
    float ref = (float)(d->rs[24] & 0xFF);
    memcpy(out + 32, &alpha_func, 4); memcpy(out + 36, &ref, 4); memcpy(out + 40, &fog, 4);
    for (int s = 0; s < 8; s++) color4(d->tss[s][32], f + 12 + 4 * s);
}
