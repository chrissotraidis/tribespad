/* HaloPad Direct3D 9, part 4 (G4): draws on Metal (programmable pipeline).
 *
 * A draw turns the device state into Metal objects: the vertex declaration (or FVF) and the
 * vertex shader's dcl inputs become a vertex descriptor (inputs with no element read
 * (0, 0, 0, 1)); render states become blend, depth/stencil, cull, fill and viewport; bound
 * textures and sampler states become Metal textures and samplers. Buffer and texture
 * contents are uploaded when dirty, always into a new Metal object so a buffer Halo
 * rewrites later in the frame never changes an earlier draw. Direct3D 9 specifics:
 *   - pixel centres: vertex positions move by half a pixel (+1/w, -1/h in clip space);
 *   - DEPTHBIAS is in window-depth units, so it is added to clip z in the vertex shader;
 *     SLOPESCALEDEPTHBIAS goes to Metal's slope bias;
 *   - front faces are clockwise; CULL_CW/CULL_CCW cull that winding;
 *   - draws outside BeginScene/EndScene are invalid; *UP draws reset stream 0 (and the
 *     index buffer) afterwards; triangle fans become triangle lists.
 * Draws without a vertex or pixel shader use the generated fixed-function programs
 * (halopad_d3d9_ff.c). States outside this set stop with a message naming them. */
#include "halopad_win32.h"
#include "halopad_d3d9_internal.h"
int halopad_metal_supports_bc(void);
#include "halopad_metal.h"
int halopad_d3d9_tracing(void);
void halopad_d3d9_trace_callers(char *out, size_t n);
extern uint32_t halopad_d3d9_frame;

void *halopad_com_state(const char *iface, uint32_t g);
const char *halopad_com_interface(uint32_t g);
void halopad_com_bind(uint32_t g);
void halopad_com_unbind(uint32_t g);

typedef struct { uint8_t sampler_dim[16]; uint8_t projected[16]; uint8_t test_kernel; } hp_shader_key;
char *halopad_shader_to_msl(const uint32_t *t, uint32_t n, const hp_shader_key *key, char *err, size_t errlen);
int halopad_shader_vs_inputs(const uint32_t *t, uint32_t n, uint8_t usage[16], uint8_t index[16], uint8_t used[16]);

/* fixed function (halopad_d3d9_ff.c) */
typedef struct {
    uint8_t pretransformed, has_normal, has_color[2], has_psize, tex_size[8];
    uint8_t lighting, normalize, local_viewer, light_type[8];
    uint8_t src_diffuse, src_specular, src_ambient, src_emissive;
    uint8_t fog_mode, range_fog;
    uint8_t stage_index[8], stage_gen[8], stage_count[8], stage_proj[8];
} hp_ff_vs_key;
typedef struct {
    struct { uint8_t cop, carg[3], aop, aarg[3], result, tex_dim, projected; } st[8];
    uint8_t specular;
} hp_ff_ps_key;
char *halopad_ff_vs_msl(const hp_ff_vs_key *k);
char *halopad_ff_ps_msl(const hp_ff_ps_key *k, char *err, size_t errlen);
void halopad_ff_vs_key(device *d, const uint8_t (*el)[3], int ne, hp_ff_vs_key *k, uint8_t in_usage[16], uint8_t in_index[16],
                       uint8_t in_used[16]);
void halopad_ff_vs_constants(device *d, uint8_t out[1760], const float fixup[4]);
void halopad_ff_ps_key(device *d, const uint8_t tex_dim[16], hp_ff_ps_key *k);
void halopad_ff_ps_constants(device *d, uint8_t out[176]);

/* generated fixed-function programs, by key: a hash table that grows (a Slayer game on Blood
   Gulch needs more than 256 distinct stage cascades once vehicles and effects are in view) */
typedef struct ff_entry { uint8_t key[128]; uint32_t len; char *msl; struct ff_entry *next; } ff_entry;
static ff_entry *ff_buckets[4096];
static uint32_t ff_hash(const void *key, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) h = (h ^ ((const uint8_t *)key)[i]) * 16777619u;
    return h;
}
static const char *ff_lookup(const void *key, uint32_t len, char *(*make)(const void *, char *, size_t), const char *what)
{
    if (len > sizeof ff_buckets[0]->key) hp_unsupported("draw", "fixed-function key of %u bytes", len);
    ff_entry **b = &ff_buckets[ff_hash(key, len) % 4096];
    for (ff_entry *e = *b; e; e = e->next)
        if (e->len == len && !memcmp(e->key, key, len)) return e->msl;
    char err[256] = "";
    char *msl = make(key, err, sizeof err);
    if (!msl) hp_unsupported("draw", "fixed-function %s: %s", what, err);
    ff_entry *e = calloc(1, sizeof *e);
    memcpy(e->key, key, len);
    e->len = len;
    e->msl = msl;
    e->next = *b;
    *b = e;
    return msl;
}
static char *make_ff_vs(const void *k, char *e, size_t n) { (void)e; (void)n; return halopad_ff_vs_msl(k); }
static char *make_ff_ps(const void *k, char *e, size_t n) { return halopad_ff_ps_msl(k, e, n); }

#define D3D_OK 0u
#define D3DERR_INVALIDCALL 0x8876086Cu

static device *dev(uint32_t g) { return halopad_com_state("IDirect3DDevice9", g); }
static float f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* ---- shaders ---- */

static const char *vs_source(res *vs)
{
    if (!vs->msl) {
        char err[256];
        vs->msl = halopad_shader_to_msl(vs->tokens, vs->count, NULL, err, sizeof err);
        if (!vs->msl) hp_unsupported("draw", "vertex shader: %s", err);
        if (halopad_shader_vs_inputs(vs->tokens, vs->count, vs->in_usage, vs->in_index, vs->in_used) < 0)
            hp_unsupported("draw", "vertex shader input declarations");
    }
    return vs->msl;
}

static const char *ps_source(res *ps, const hp_shader_key *key)
{
    for (uint32_t i = 0; i < ps->nvariant; i++)
        if (!memcmp(ps->variant[i].key, key, sizeof *key)) return ps->variant[i].msl;
    if (ps->nvariant == 8) hp_unsupported("draw", "more than 8 sampler configurations for one pixel shader");
    char err[256];
    char *msl = halopad_shader_to_msl(ps->tokens, ps->count, key, err, sizeof err);
    if (!msl) hp_unsupported("draw", "pixel shader: %s", err);
    memcpy(ps->variant[ps->nvariant].key, key, sizeof *key);
    ps->variant[ps->nvariant].msl = msl;
    return ps->variant[ps->nvariant++].msl;
}

/* ---- vertex layout ---- */

typedef struct { uint8_t stream, type, usage, index; uint16_t offset; } element;

static int fvf_elements(uint32_t fvf, element *e, uint32_t *stride)
{
    int n = 0;
    uint16_t off = 0;
    uint32_t pos = fvf & 0x400E;
    if (pos == 0x2) { e[n++] = (element){0, 2, 0, 0, off}; off += 12; }                    /* XYZ: FLOAT3 POSITION */
    else if (pos == 0x4) { e[n++] = (element){0, 3, 9, 0, off}; off += 16; }               /* XYZRHW: FLOAT4 POSITIONT */
    else if (pos == 0x4002) { e[n++] = (element){0, 3, 0, 0, off}; off += 16; }            /* XYZW */
    else hp_unsupported("draw", "FVF position type 0x%x (pretransformed or blended vertices)", pos);
    if (fvf & 0x10) { e[n++] = (element){0, 2, 3, 0, off}; off += 12; }                    /* NORMAL */
    if (fvf & 0x20) { e[n++] = (element){0, 0, 4, 0, off}; off += 4; }                     /* PSIZE */
    if (fvf & 0x40) { e[n++] = (element){0, 4, 10, 0, off}; off += 4; }                    /* DIFFUSE: COLOR0 */
    if (fvf & 0x80) { e[n++] = (element){0, 4, 10, 1, off}; off += 4; }                    /* SPECULAR: COLOR1 */
    uint32_t ntex = (fvf >> 8) & 0xF;
    for (uint32_t i = 0; i < ntex; i++) {
        static const uint8_t type[4] = {1, 2, 3, 0}, size[4] = {8, 12, 16, 4};
        uint32_t f = (fvf >> (16 + 2 * i)) & 3;
        e[n++] = (element){0, type[f], 5, (uint8_t)i, off};
        off += size[f];
    }
    if (fvf & ~(0x400Eu | 0x10u | 0x20u | 0x40u | 0x80u | 0xF00u | 0xFFFF0000u)) hp_unsupported("draw", "FVF bits 0x%x", fvf);
    *stride = off;
    return n;
}

static int decl_elements(res *decl, element *e)
{
    int n = 0;
    for (uint32_t i = 0; i + 1 < decl->count; i++) {
        const uint8_t *b = (const uint8_t *)&decl->tokens[2 * i];
        uint16_t stream = (uint16_t)(b[0] | b[1] << 8), offset = (uint16_t)(b[2] | b[3] << 8);
        if (b[5]) hp_unsupported("draw", "declaration method %u (tessellation)", b[5]);
        e[n++] = (element){(uint8_t)stream, b[4], b[6], b[7], offset};
    }
    return n;
}

static uint8_t metal_vertex_format(uint8_t t)
{
    /* D3DDECLTYPE -> MTLVertexFormat */
    static const uint8_t m[17] = {28 /* FLOAT1: Float */, 29 /* FLOAT2 */, 30 /* FLOAT3 */, 31 /* FLOAT4 */,
                                  42 /* D3DCOLOR: UChar4Normalized_BGRA */, 3 /* UBYTE4: UChar4 */, 16 /* SHORT2: Short2 */,
                                  18 /* SHORT4: Short4 */, 9 /* UBYTE4N: UChar4Normalized */, 22 /* SHORT2N */,
                                  24 /* SHORT4N */, 19 /* USHORT2N */, 21 /* USHORT4N */, 0 /* UDEC3: none */,
                                  40 /* DEC3N: Int1010102Normalized */, 25 /* FLOAT16_2: Half2 */, 27 /* FLOAT16_4: Half4 */};
    if (t > 16 || !m[t]) hp_unsupported("draw", "vertex element type %u", t);
    return m[t];
}

/* ---- uploads ---- */

static void *upload_buffer(res *r)
{
    if (r->dirty[0] || !r->native) {
        halopad_metal_release(r->native);
        r->native = halopad_metal_buffer(G(r->buf), r->length);
        r->dirty[0] = 0;
    }
    return r->native;
}

static void texture_format(uint32_t f, uint32_t *mtl, uint8_t sw[4], int *convert)
{
    /* MTLTextureSwizzle: Zero 0, One 1, Red 2, Green 3, Blue 4, Alpha 5 */
    uint8_t id[4] = {2, 3, 4, 5};
    memcpy(sw, id, 4);
    *convert = 0;
    switch (f) {
    case 21: *mtl = 80; return;                                     /* A8R8G8B8: BGRA8Unorm */
    case 22: *mtl = 80; sw[3] = 1; return;                          /* X8R8G8B8: alpha reads 1 */
    case 23: case 24: case 25: case 26: *mtl = 80; *convert = 1; return;   /* 16-bit colour: expanded to BGRA8 */
    case 28: *mtl = 1; return;                                      /* A8: A8Unorm */
    case 50: *mtl = 10; sw[1] = 2; sw[2] = 2; sw[3] = 1; return;    /* L8: R8Unorm as (L, L, L, 1) */
    case 51: *mtl = 30; sw[1] = 2; sw[2] = 2; sw[3] = 3; return;    /* A8L8: RG8Unorm as (L, L, L, A) */
    case 60: *mtl = 32; sw[2] = 1; sw[3] = 1; return;               /* V8U8: RG8Snorm as (U, V, 1, 1) */
    case 63: *mtl = 72; return;                                     /* Q8W8V8U8: RGBA8Snorm */
    case 0x31545844u: case 0x32545844u: case 0x33545844u: case 0x34545844u: case 0x35545844u:
        if (!halopad_metal_supports_bc()) { *mtl = 80; *convert = 2; return; }   /* decoded to BGRA8 (dxt_decode) */
        *mtl = f == 0x31545844u ? 130 : (f == 0x32545844u || f == 0x33545844u) ? 132 : 134;   /* BC1/BC2/BC3_RGBA */
        return;
    }
    hp_unsupported("draw", "texture format %u", f);
}

static void expand16(uint32_t f, const uint16_t *src, uint8_t *dst, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint16_t p = src[i];
        uint32_t r, g, b, a;
        if (f == 23) { r = p >> 11; g = (p >> 5) & 63; b = p & 31; r = r * 255 / 31; g = g * 255 / 63; b = b * 255 / 31; a = 255; }
        else if (f == 26) { a = (p >> 12) * 17; r = ((p >> 8) & 15) * 17; g = ((p >> 4) & 15) * 17; b = (p & 15) * 17; }
        else { r = ((p >> 10) & 31) * 255 / 31; g = ((p >> 5) & 31) * 255 / 31; b = (p & 31) * 255 / 31; a = (f == 24 || (p >> 15)) ? 255 : 0; }
        dst[4 * i] = (uint8_t)b; dst[4 * i + 1] = (uint8_t)g; dst[4 * i + 2] = (uint8_t)r; dst[4 * i + 3] = (uint8_t)a;
    }
}

/* DXT (S3TC) decoding for GPUs without BC textures (the iPad Simulator and iPads before M1;
   docs/G3-RUNTIME.md): each 4x4 block becomes BGRA8, as the BC1-BC3 formats define it. DXT2
   and DXT4 (premultiplied) decode as DXT3 and DXT5, as Metal's BC2 and BC3 would read them. */
static void dxt_color(const uint8_t *b, uint8_t out[16][4], int four_color)
{
    uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    uint32_t idx = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    uint8_t p[4][4];
    uint32_t r0 = (c0 >> 11) * 255 / 31, g0 = ((c0 >> 5) & 63) * 255 / 63, b0 = (c0 & 31) * 255 / 31;
    uint32_t r1 = (c1 >> 11) * 255 / 31, g1 = ((c1 >> 5) & 63) * 255 / 63, b1 = (c1 & 31) * 255 / 31;
    p[0][0] = (uint8_t)b0; p[0][1] = (uint8_t)g0; p[0][2] = (uint8_t)r0; p[0][3] = 255;
    p[1][0] = (uint8_t)b1; p[1][1] = (uint8_t)g1; p[1][2] = (uint8_t)r1; p[1][3] = 255;
    if (four_color || c0 > c1) {
        p[2][0] = (uint8_t)((2 * b0 + b1) / 3); p[2][1] = (uint8_t)((2 * g0 + g1) / 3); p[2][2] = (uint8_t)((2 * r0 + r1) / 3); p[2][3] = 255;
        p[3][0] = (uint8_t)((b0 + 2 * b1) / 3); p[3][1] = (uint8_t)((g0 + 2 * g1) / 3); p[3][2] = (uint8_t)((r0 + 2 * r1) / 3); p[3][3] = 255;
    } else {
        p[2][0] = (uint8_t)((b0 + b1) / 2); p[2][1] = (uint8_t)((g0 + g1) / 2); p[2][2] = (uint8_t)((r0 + r1) / 2); p[2][3] = 255;
        p[3][0] = p[3][1] = p[3][2] = p[3][3] = 0;                          /* transparent black */
    }
    for (int i = 0; i < 16; i++) memcpy(out[i], p[(idx >> (2 * i)) & 3], 4);
}

static void dxt_block(uint32_t f, const uint8_t *b, uint8_t out[16][4])
{
    if (f == 0x31545844u) { dxt_color(b, out, 0); return; }                  /* DXT1 */
    dxt_color(b + 8, out, 1);
    if (f == 0x32545844u || f == 0x33545844u) {                              /* DXT2/3: explicit 4-bit alpha */
        for (int i = 0; i < 16; i++) out[i][3] = (uint8_t)(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
        return;
    }
    uint32_t a0 = b[0], a1 = b[1], a[8];                                     /* DXT4/5: interpolated alpha */
    a[0] = a0; a[1] = a1;
    if (a0 > a1) for (int i = 1; i < 7; i++) a[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    else { for (int i = 1; i < 5; i++) a[i + 1] = ((5 - i) * a0 + i * a1) / 5; a[6] = 0; a[7] = 255; }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) out[i][3] = (uint8_t)a[(bits >> (3 * i)) & 7];
}

/* one level's slice of blocks (rows of pitch bytes) into w x h BGRA8 */
/* HALOPAD_TRACE_FRAMES counters (reported by halopad_metal_present) */
double halopad_trace_now(void);
extern int halopad_trace_decodes;
extern double halopad_trace_decode_s, halopad_trace_decode_px;

static void dxt_decode(uint32_t f, const uint8_t *src, uint32_t pitch, uint8_t *dst, uint32_t w, uint32_t h)
{
    uint32_t bs = f == 0x31545844u ? 8 : 16;
    for (uint32_t by = 0; by < (h + 3) / 4; by++)
        for (uint32_t bx = 0; bx < (w + 3) / 4; bx++) {
            uint8_t px[16][4];
            dxt_block(f, src + by * pitch + bx * bs, px);
            for (uint32_t y = 0; y < 4 && 4 * by + y < h; y++)
                for (uint32_t x = 0; x < 4 && 4 * bx + x < w; x++)
                    memcpy(dst + 4 * ((4 * by + y) * w + 4 * bx + x), px[4 * y + x], 4);
        }
}

static void *upload_texture(res *t)
{
    uint32_t mtl;
    uint8_t sw[4];
    int convert;
    if (t->usage & 0x1) return halopad_d3d9_rt_view(t);             /* render target: its contents are on the GPU */
    texture_format(t->format, &mtl, sw, &convert);
    int type = t->ttype ? (int)t->ttype : 2;
    uint32_t faces = type == 3 ? 6 : 1;
    if (!t->native) t->native = halopad_metal_texture(type, mtl, t->width, t->height, type == 4 ? t->depth : 1, t->levels, sw);
    for (uint32_t l = 0; l < t->levels; l++) {
        if (!t->dirty[l]) continue;
        if (!t->mem[l]) hp_unsupported("draw", "a texture whose contents live only on the GPU (render target or default pool)");
        uint32_t depth = type == 4 ? t->ld[l] : 1, slice = type == 4 ? t->slice[l] : t->size[l];
        for (uint32_t f = 0; f < faces; f++) {
            const uint8_t *src = (const uint8_t *)G(t->mem[l]) + f * t->size[l];
            if (convert == 2) {
                uint32_t n = t->lw[l] * t->lh[l];
                uint8_t *tmp = malloc(4 * n * depth);
                double t0 = halopad_trace_now();
                for (uint32_t z = 0; z < depth; z++) dxt_decode(t->format, src + z * slice, t->pitch[l], tmp + 4 * n * z, t->lw[l], t->lh[l]);
                halopad_trace_decodes++; halopad_trace_decode_s += halopad_trace_now() - t0; halopad_trace_decode_px += n * depth;
                halopad_metal_texture_upload(t->native, l, f, tmp, 4 * t->lw[l], 4 * n, t->lw[l], t->lh[l], depth);
                free(tmp);
            } else if (convert) {
                uint32_t n = t->lw[l] * t->lh[l];
                uint8_t *tmp = malloc(4 * n * depth);
                for (uint32_t z = 0; z < depth; z++)
                    for (uint32_t y = 0; y < t->lh[l]; y++)
                        expand16(t->format, (const uint16_t *)(src + z * slice + y * t->pitch[l]), tmp + 4 * (n * z + t->lw[l] * y), t->lw[l]);
                halopad_metal_texture_upload(t->native, l, f, tmp, 4 * t->lw[l], 4 * n, t->lw[l], t->lh[l], depth);
                free(tmp);
            } else {
                halopad_metal_texture_upload(t->native, l, f, src, t->pitch[l], slice, t->lw[l], t->lh[l], depth);
            }
        }
        t->dirty[l] = 0;
    }
    return t->native;
}
void *halopad_d3d9_upload_texture(res *t) { return upload_texture(t); }

static void *sampler(device *d, int s)
{
    const uint32_t *v = d->ss[s];
    hp_sampler_desc sd = {0};
    uint32_t mag = v[5], min = v[6], mip = v[7];
    if (mag > 3 || min > 3 || mip > 2) hp_unsupported("draw", "sampler %d filters %u/%u/%u", s, mag, min, mip);
    sd.mag = mag >= 2; sd.min = min >= 2;
    sd.mip = mip == 0 ? 0 : mip == 1 ? 1 : 2;                       /* MTLSamplerMipFilter: NotMipmapped, Nearest, Linear */
    sd.anisotropy = (uint8_t)((min == 3 || mag == 3) ? (v[10] > 16 ? 16 : v[10]) : 1);
    uint8_t *addr[3] = {&sd.u, &sd.v, &sd.w};
    for (int k = 0; k < 3; k++) {
        switch (v[1 + k]) {
        case 1: *addr[k] = 2; break;                                /* WRAP: Repeat */
        case 2: *addr[k] = 3; break;                                /* MIRROR: MirrorRepeat */
        case 3: *addr[k] = 0; break;                                /* CLAMP: ClampToEdge */
        case 4:                                                     /* BORDER: ClampToBorderColor */
            *addr[k] = 5;
            if (v[4] == 0) sd.border = 0;
            else if (v[4] == 0xFF000000u) sd.border = 1;
            else if (v[4] == 0xFFFFFFFFu) sd.border = 2;
            else hp_unsupported("draw", "border colour 0x%08x (Metal offers transparent black, opaque black, opaque white)", v[4]);
            break;
        case 5: *addr[k] = 1; break;                                /* MIRRORONCE: MirrorClampToEdge */
        default: hp_unsupported("draw", "sampler %d address mode %u", s, v[1 + k]);
        }
    }
    if (v[8]) hp_unsupported("draw", "MIPMAPLODBIAS %g", f32(v[8]));
    sd.lod_min = (float)v[9];                                       /* MAXMIPLEVEL: the largest level used */
    return halopad_metal_sampler(&sd);
}

/* ---- render state -> pipeline / depth-stencil ---- */

static uint8_t blend_factor(uint32_t b)
{
    static const uint8_t m[16] = {0, 0, 1, 2, 3, 4, 5, 8, 9, 6, 7, 10, 4, 5, 11, 12};
    if (b < 1 || b > 15) hp_unsupported("draw", "blend factor %u", b);
    return m[b];
}
static uint8_t blend_op(uint32_t o) { if (o < 1 || o > 5) hp_unsupported("draw", "blend op %u", o); return (uint8_t)(o - 1); }
static uint8_t compare_fn(uint32_t c) { if (c < 1 || c > 8) hp_unsupported("draw", "compare function %u", c); return (uint8_t)(c - 1); }
static uint8_t stencil_op(uint32_t o) { if (o < 1 || o > 8) hp_unsupported("draw", "stencil op %u", o); return (uint8_t)(o - 1); }

static void check_states(device *d)
{
    const uint32_t *rs = d->rs;
    if (rs[8] != 3 && rs[8] != 2) hp_unsupported("draw", "FILLMODE %u", rs[8]);
    if (rs[152]) hp_unsupported("draw", "user clip planes (0x%x)", rs[152]);
    if (rs[28] && rs[35] > 3) hp_unsupported("draw", "table (per-pixel) fog mode %u", rs[35]);
    if (rs[194]) hp_unsupported("draw", "sRGB writes");
    if (rs[161] != 1 && rs[161] != 0) hp_unsupported("draw", "MULTISAMPLEANTIALIAS %u", rs[161]);
    /* POINTSPRITEENABLE/POINTSCALEENABLE affect only point lists (refused in draw);
       Halo's state reset turns sprites on for every draw (0x519bf9). */
    if (rs[167] || rs[151]) hp_unsupported("draw", "vertex blending in fixed function");
}

/* ---- the draw ---- */

static uint32_t vertex_count(uint32_t type, uint32_t prims)
{
    switch (type) {
    case 1: return prims; case 2: return 2 * prims; case 3: return prims + 1;
    case 4: return 3 * prims; case 5: case 6: return prims + 2;
    }
    return 0;
}

static uint8_t metal_prim(uint32_t type)
{
    static const uint8_t m[7] = {0, 0 /* point */, 1 /* line */, 2 /* line strip */, 3 /* triangle */, 4 /* strip */, 3 /* fan -> list */};
    return m[type];
}

/* Encode one draw. up: vertex data (guest address) and stride for *UP draws, or 0.
   Indices: ib (resource) or up_indices (guest address), or neither. */
static uint32_t draw(device *d, uint32_t type, uint32_t prims, uint32_t start, int32_t base, uint32_t up, uint32_t up_stride,
                     res *ib, uint32_t up_indices, uint32_t up_index32, uint32_t first_index)
{
    if (!d->in_scene) return D3DERR_INVALIDCALL;
    if (type < 1 || type > 6 || !prims) return D3DERR_INVALIDCALL;
    if (type == 1) hp_unsupported("draw", "point lists (point size, sprites 0x%x, scale 0x%x)", d->rs[156], d->rs[157]);
    if (halopad_d3d9_tracing()) {
        static uint32_t n, last_frame;
        if (last_frame != halopad_d3d9_frame) { last_frame = halopad_d3d9_frame; n = 0; }
        char callers[64];
        halopad_d3d9_trace_callers(callers, sizeof callers);
        const uint32_t *rs = d->rs;
        fprintf(stderr, "HALOPAD DRAW f%u #%u type %u prims %u%s%s vs %08x ps %08x fvf %x rt %08x blend %u %u/%u op %u z %u/%u func %u atest %u/%u ref %u cw %x"
                " tex %08x %08x %08x %08x vp %u,%u %ux%u from%s\n",
                halopad_d3d9_frame, n++, type, prims, up ? " UP" : "", ib ? " indexed" : "", d->vs, d->ps, d->fvf, d->rt,
                rs[27], rs[19], rs[20], rs[171], rs[7], rs[14], rs[23], rs[15], rs[25], rs[24] & 0xFF, rs[168],
                d->texture[0], d->texture[1], d->texture[2], d->texture[3],
                d->viewport[0], d->viewport[1], d->viewport[2], d->viewport[3], callers);
        fprintf(stderr, "HALOPAD DRAW   depth %g..%g bias %g slope %g stencil %u fog %u/%08x/%u scissor %u clip %u\n", f32(d->viewport[4]), f32(d->viewport[5]),
                f32(rs[195]), f32(rs[175]), rs[52], rs[28], rs[34], rs[35], rs[174], rs[136]);
        if (up) {
            for (uint32_t i = 0; i < (prims <= 2 ? prims + 2 : 1); i++) {
                const uint32_t *w = (const uint32_t *)G(up + i * up_stride);
                const float *v = (const float *)w;
                fprintf(stderr, "HALOPAD DRAW   vertex %u:", i);
                for (uint32_t k = 0; k < up_stride / 4 && k < 8; k++) fprintf(stderr, " %g/%08x", v[k], w[k]);
                fprintf(stderr, "\n");
            }
        }
        if (d->vs && getenv("HALOPAD_TRACE_DRAWS_VSCONSTS"))
            for (int c = 0; c < 12; c++) fprintf(stderr, "HALOPAD DRAW   vs c%d = %g %g %g %g\n", c, d->vsf[c][0], d->vsf[c][1], d->vsf[c][2], d->vsf[c][3]);
        if (d->ps && getenv("HALOPAD_TRACE_DRAWS_CONSTS"))
            for (int c = 0; c < 8; c++) fprintf(stderr, "HALOPAD DRAW   ps c%d = %g %g %g %g\n", c, d->psf[c][0], d->psf[c][1], d->psf[c][2], d->psf[c][3]);
    }
    check_states(d);
    hp_bound bound = halopad_d3d9_bind_targets(d);
    hp_pipeline_desc pd;
    memset(&pd, 0, sizeof pd);
    hp_draw_desc dd;
    memset(&dd, 0, sizeof dd);

    /* bound textures (upload) and their sampler states */
    hp_shader_key key;
    memset(&key, 0, sizeof key);
    uint8_t tex_dim[16] = {0};
    for (int s = 0; s < 16; s++) {
        if (!d->texture[s]) continue;
        res *t = halopad_com_state(halopad_com_interface(d->texture[s]), d->texture[s]);
        key.sampler_dim[s] = (uint8_t)t->ttype;
        tex_dim[s] = (uint8_t)t->ttype;
        if (s < 8 && (d->tss[s][24] & 0x100)) key.projected[s] = 1;   /* D3DTTFF_PROJECTED */
        dd.tex[s] = upload_texture(t);
        dd.smp[s] = sampler(d, s);
    }
    res *ps = d->ps ? halopad_com_state("IDirect3DPixelShader9", d->ps) : NULL;
    if (ps) {
        pd.ps_msl = ps_source(ps, &key);
    } else {
        hp_ff_ps_key fk;
        halopad_ff_ps_key(d, tex_dim, &fk);
        pd.ps_msl = ff_lookup(&fk, sizeof fk, make_ff_ps, "pixel processing");
    }

    /* vertex layout: the vertex shader's dcl inputs, or the fixed-function inputs present */
    element e[64];
    int ne;
    uint32_t fvf_stride = 0;
    if (d->decl) ne = decl_elements(halopad_com_state("IDirect3DVertexDeclaration9", d->decl), e);
    else if (d->fvf) ne = fvf_elements(d->fvf, e, &fvf_stride);
    else hp_unsupported("draw", "no vertex declaration or FVF");
    res *vs = d->vs ? halopad_com_state("IDirect3DVertexShader9", d->vs) : NULL;
    uint8_t in_usage[16], in_index[16], in_used[16];
    hp_ff_vs_key fvk;
    if (vs) {
        pd.vs_msl = vs_source(vs);
        memcpy(in_usage, vs->in_usage, 16); memcpy(in_index, vs->in_index, 16); memcpy(in_used, vs->in_used, 16);
    } else {
        static const uint8_t comps[17] = {1, 2, 3, 4, 4, 4, 2, 4, 4, 2, 4, 2, 4, 3, 3, 2, 4};
        uint8_t el[64][3];
        for (int i = 0; i < ne; i++) { el[i][0] = e[i].usage; el[i][1] = e[i].index; el[i][2] = e[i].type <= 16 ? comps[e[i].type] : 4; }
        halopad_ff_vs_key(d, (const uint8_t (*)[3])el, ne, &fvk, in_usage, in_index, in_used);
        pd.vs_msl = ff_lookup(&fvk, sizeof fvk, make_ff_vs, "vertex processing");
    }
    for (int r = 0; r < 16; r++) {
        if (!in_used[r]) continue;
        int found = -1;
        for (int i = 0; i < ne; i++) if (e[i].usage == in_usage[r] && e[i].index == in_index[r]) { found = i; break; }
        if (found < 0) { pd.constant_regs |= 1u << r; continue; }
        pd.attr[pd.nattr++] = (hp_vattr){e[found].stream, metal_vertex_format(e[found].type), (uint8_t)r, 0, e[found].offset};
        uint32_t st = e[found].stream;
        if (up) { if (st) hp_unsupported("draw", "a *UP draw using stream %u", st); pd.stride[0] = up_stride; }
        else {
            if (!d->stream[st]) hp_unsupported("draw", "no vertex buffer on stream %u", st);
            pd.stride[st] = d->stream_stride[st];
            if (!dd.vbuf[st]) {
                dd.vbuf[st] = upload_buffer(halopad_com_state("IDirect3DVertexBuffer9", d->stream[st]));
                dd.voff[st] = d->stream_offset[st];
            }
        }
    }
    uint32_t nverts = vertex_count(type, prims);
    uint32_t toff = 0;                                              /* transient data lives until Present (halopad_metal_temp) */
    if (up) {
        uint32_t span = ib || up_indices ? 0 : nverts;
        if (up_indices) span = start + (uint32_t)base;              /* MinVertexIndex + NumVertices, passed in */
        dd.vbuf[0] = halopad_metal_temp(G(up), span * up_stride, &toff);
        dd.voff[0] = toff;
    }

    /* render state */
    const uint32_t *rs = d->rs;
    pd.blend = rs[27] != 0;
    uint32_t sb = rs[19], db = rs[20];
    if (sb == 12) { sb = 5; db = 6; } else if (sb == 13) { sb = 6; db = 5; }   /* BOTH(INV)SRCALPHA */
    pd.src_rgb = blend_factor(sb); pd.dst_rgb = blend_factor(db); pd.op_rgb = blend_op(rs[171]);
    if (rs[206]) { pd.src_a = blend_factor(rs[207]); pd.dst_a = blend_factor(rs[208]); pd.op_a = blend_op(rs[209]); }
    else { pd.src_a = pd.src_rgb; pd.dst_a = pd.dst_rgb; pd.op_a = pd.op_rgb; }
    uint32_t cw = rs[168];                                          /* D3D R1 G2 B4 A8 -> Metal A1 B2 G4 R8 */
    pd.write_mask = (uint8_t)(((cw & 1) << 3) | ((cw & 2) << 1) | ((cw & 4) >> 1) | ((cw & 8) >> 3));
    if (bound.format == 22) pd.write_mask &= (uint8_t)~1u;          /* X8R8G8B8: alpha stays 1, as Direct3D reads it */
    pd.depth = (uint8_t)bound.depth;
    pd.color_format = bound.mtl;
    char err[512];
    dd.pipeline = halopad_metal_pipeline(&pd, err, sizeof err);
    if (!dd.pipeline) hp_unsupported("draw", "Metal pipeline: %s", err);
    if (pd.depth) {
        hp_depth_desc ds;
        memset(&ds, 0, sizeof ds);
        ds.depth_func = rs[7] ? compare_fn(rs[23]) : 7;
        ds.depth_write = rs[7] && rs[14];
        if (rs[52]) {
            ds.stencil = 1;
            int two = rs[185] != 0;
            ds.fail[0] = stencil_op(rs[53]); ds.zfail[0] = stencil_op(rs[54]); ds.pass[0] = stencil_op(rs[55]); ds.func[0] = compare_fn(rs[56]);
            ds.fail[1] = two ? stencil_op(rs[186]) : ds.fail[0]; ds.zfail[1] = two ? stencil_op(rs[187]) : ds.zfail[0];
            ds.pass[1] = two ? stencil_op(rs[188]) : ds.pass[0]; ds.func[1] = two ? compare_fn(rs[189]) : ds.func[0];
            ds.read_mask = rs[58]; ds.write_mask = rs[59];
        }
        dd.depth_state = halopad_metal_depth_state(&ds);
    }
    dd.cull = rs[22] == 1 ? 0 : rs[22] == 2 ? 1 : 2;                /* NONE, CW (front), CCW (back) */
    dd.front_cw = 1;
    dd.lines = rs[8] == 2;
    dd.slope_bias = f32(rs[175]);
    dd.stencil_ref = rs[57];
    uint32_t bf = rs[193];
    dd.blend_color[0] = ((bf >> 16) & 0xFF) / 255.0f; dd.blend_color[1] = ((bf >> 8) & 0xFF) / 255.0f;
    dd.blend_color[2] = (bf & 0xFF) / 255.0f; dd.blend_color[3] = (bf >> 24) / 255.0f;
    const uint32_t *vp = d->viewport;
    float minz = f32(vp[4]), maxz = f32(vp[5]);
    dd.viewport[0] = vp[0]; dd.viewport[1] = vp[1]; dd.viewport[2] = vp[2]; dd.viewport[3] = vp[3];
    dd.viewport[4] = minz; dd.viewport[5] = maxz;
    if (rs[174]) {                                                  /* SCISSORTESTENABLE: the rectangle, inside the target */
        const uint32_t *sr = d->scissor;
        uint32_t x0 = sr[0], y0 = sr[1], x1 = sr[2], y1 = sr[3];
        if (!(x0 | y0 | x1 | y1)) { x1 = bound.width; y1 = bound.height; }
        if (x1 > bound.width) x1 = bound.width;
        if (y1 > bound.height) y1 = bound.height;
        if (x0 >= x1) x0 = x1 ? x1 - 1 : 0;
        if (y0 >= y1) y0 = y1 ? y1 - 1 : 0;
        dd.scissor = 1;
        dd.scissor_rect[0] = x0; dd.scissor_rect[1] = y0; dd.scissor_rect[2] = x1 - x0 ? x1 - x0 : 1; dd.scissor_rect[3] = y1 - y0 ? y1 - y0 : 1;
    }

    /* constants: shaders get c/i/b plus the fixup (pixel centre, depth bias) and fog/alpha
       test; fixed function gets its transforms, material, lights and stage constants */
    static uint8_t vsc[4448], psc[3936], ffv[1760], ffp[176];
    float fix[4] = {1.0f / (float)vp[2], -1.0f / (float)vp[3], maxz > minz ? f32(rs[195]) / (maxz - minz) : 0.0f, 0.0f};
    if (vs) {
        memcpy(vsc, d->vsf, 4096); memcpy(vsc + 4096, d->vsi, 256); memcpy(vsc + 4352, d->vsb, 64);
        memcpy(vsc + 4416, fix, 16);
        float tfog[4] = {rs[28] ? (float)rs[35] : 0.0f, f32(rs[36]), f32(rs[37]), f32(rs[38])};   /* table fog: mode, start, end, density */
        memcpy(vsc + 4432, tfog, 16);
        dd.vs_consts = vsc; dd.vs_len = sizeof vsc;
    } else {
        halopad_ff_vs_constants(d, ffv, fix);
        dd.vs_consts = ffv; dd.vs_len = sizeof ffv;
    }
    if (ps) {
        memcpy(psc, d->psf, 3584); memcpy(psc + 3584, d->psi, 256); memcpy(psc + 3840, d->psb, 64);
        uint32_t fc = rs[34];
        float fogc[4] = {((fc >> 16) & 0xFF) / 255.0f, ((fc >> 8) & 0xFF) / 255.0f, (fc & 0xFF) / 255.0f, 1.0f};
        memcpy(psc + 3904, fogc, 16);
        uint32_t alpha_func = rs[15] ? rs[25] : 8u, fog = rs[28] ? 1u : 0u;
        float alpha_ref = (float)(rs[24] & 0xFF);
        memcpy(psc + 3920, &alpha_func, 4); memcpy(psc + 3924, &alpha_ref, 4); memcpy(psc + 3928, &fog, 4);
        dd.ps_consts = psc; dd.ps_len = sizeof psc;
    } else {
        halopad_ff_ps_constants(d, ffp);
        dd.ps_consts = ffp; dd.ps_len = sizeof ffp;
    }

    /* primitive and indices */
    dd.prim = metal_prim(type);
    if (ib || up_indices) {
        int i32 = ib ? ib->format == 102 : (int)up_index32;
        uint32_t isz = i32 ? 4 : 2;
        const uint8_t *src = ib ? (const uint8_t *)G(ib->buf) + first_index * isz : (const uint8_t *)G(up_indices);
        if (type == 6) {                                            /* fan -> list */
            uint32_t n = 3 * prims;
            uint32_t *list = malloc(4 * n);
            for (uint32_t i = 0; i < prims; i++) {
                uint32_t idx[3] = {0, i + 1, i + 2};
                for (int k = 0; k < 3; k++) list[3 * i + k] = i32 ? ((const uint32_t *)src)[idx[k]] : ((const uint16_t *)src)[idx[k]];
            }
            dd.ibuf = halopad_metal_temp(list, 4 * n, &toff);
            free(list);
            dd.index32 = 1; dd.index_offset = toff; dd.count = n;
        } else if (ib) {
            dd.ibuf = upload_buffer(ib); dd.index32 = (uint8_t)i32; dd.index_offset = first_index * isz; dd.count = nverts;
        } else {
            dd.ibuf = halopad_metal_temp(src, nverts * isz, &toff);
            dd.index32 = (uint8_t)i32; dd.index_offset = toff; dd.count = nverts;
        }
        dd.base_vertex = ib ? base : 0;
    } else if (type == 6) {                                         /* non-indexed fan -> indexed list */
        uint32_t n = 3 * prims;
        uint32_t *list = malloc(4 * n);
        for (uint32_t i = 0; i < prims; i++) { list[3 * i] = start; list[3 * i + 1] = start + i + 1; list[3 * i + 2] = start + i + 2; }
        dd.ibuf = halopad_metal_temp(list, 4 * n, &toff);
        free(list);
        dd.index32 = 1; dd.index_offset = toff; dd.count = n;
    } else {
        dd.start = up ? 0 : start;
        dd.count = nverts;
    }
    halopad_metal_draw(d->target, &dd);
    return D3D_OK;
}

uint32_t hpcom_IDirect3DDevice9_DrawPrimitive_c(uint32_t g, uint32_t type, uint32_t start, uint32_t prims)
{
    return draw(dev(g), type, prims, start, 0, 0, 0, NULL, 0, 0, 0);
}

uint32_t hpcom_IDirect3DDevice9_DrawIndexedPrimitive_c(uint32_t g, uint32_t type, uint32_t base, uint32_t min_index,
                                                       uint32_t nverts, uint32_t start_index, uint32_t prims)
{
    device *d = dev(g);
    (void)min_index; (void)nverts;
    if (!d->indices) return D3DERR_INVALIDCALL;
    return draw(d, type, prims, 0, (int32_t)base, 0, 0, halopad_com_state("IDirect3DIndexBuffer9", d->indices), 0, 0, start_index);
}

uint32_t hpcom_IDirect3DDevice9_DrawPrimitiveUP_c(uint32_t g, uint32_t type, uint32_t prims, uint32_t data, uint32_t stride)
{
    device *d = dev(g);
    if (!data || !stride) return D3DERR_INVALIDCALL;
    uint32_t r = draw(d, type, prims, 0, 0, data, stride, NULL, 0, 0, 0);
    halopad_com_unbind(d->stream[0]); d->stream[0] = 0; d->stream_offset[0] = 0; d->stream_stride[0] = 0;   /* D3D resets stream 0 */
    return r;
}

uint32_t hpcom_IDirect3DDevice9_DrawIndexedPrimitiveUP_c(uint32_t g, uint32_t type, uint32_t min_index, uint32_t nverts,
                                                         uint32_t prims, uint32_t indices, uint32_t format, uint32_t data,
                                                         uint32_t stride)
{
    device *d = dev(g);
    if (!data || !stride || !indices || (format != 101 && format != 102)) return D3DERR_INVALIDCALL;
    /* the vertex span is min_index + nverts; passed through 'start'/'base' for the upload */
    uint32_t r = draw(d, type, prims, min_index + nverts, 0, data, stride, NULL, indices, format == 102, 0);
    halopad_com_unbind(d->stream[0]); d->stream[0] = 0; d->stream_offset[0] = 0; d->stream_stride[0] = 0;
    halopad_com_unbind(d->indices); d->indices = 0;
    return r;
}
