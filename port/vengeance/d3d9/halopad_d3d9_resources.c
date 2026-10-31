/* HaloPad Direct3D 9, part 3 (G3/G4): resources.
 *
 * Textures (mip chains, the contract's formats including DXT), their level surfaces,
 * vertex and index buffers, vertex declarations and shaders. Resource contents live in
 * guest memory, so LockRect/Lock hand Halo a guest pointer; unlocking marks the range
 * dirty for upload to Metal when a draw uses it. Surfaces of a texture share the
 * texture's reference count, and GetContainer returns the texture, as in Direct3D 9.
 * Shader bytecode is validated token by token and kept; with HALOPAD_SHADER_DUMP set to
 * a directory each shader is also written there for analysis. */
#include "halopad_win32.h"
#include <CommonCrypto/CommonDigest.h>

uint32_t halopad_com_new(const char *iface, uint32_t size, void *state, void (*destroy)(void *));
uint32_t halopad_com_addref(uint32_t g);
uint32_t halopad_com_release(uint32_t g);
void *halopad_com_state(const char *iface, uint32_t g);
const char *halopad_com_interface(uint32_t g);

#define D3D_OK 0u
#define D3DERR_INVALIDCALL 0x8876086Cu
#include "halopad_d3d9_internal.h"
void halopad_metal_release(void *o);

/* bytes per block and block edge for a format; 0 if the contract does not offer it */
static int fmt(uint32_t f, uint32_t *bytes, uint32_t *block)
{
    *block = 1;
    switch (f) {
    case 21: case 22: case 63: case 75: case 77: *bytes = 4; return 1;
    case 23: case 24: case 25: case 26: case 51: case 60: case 80: *bytes = 2; return 1;
    case 28: case 50: *bytes = 1; return 1;
    }
    if ((f & 0x00FFFFFFu) == 0x00545844u && f >= 0x31545844u && f <= 0x35545844u) {
        *block = 4;
        *bytes = f == 0x31545844u ? 8 : 16;
        return 1;
    }
    return 0;
}
int halopad_d3d9_format_size(uint32_t f, uint32_t *bytes, uint32_t *block) { return fmt(f, bytes, block); }

/* Live default-pool resources per device: Reset refuses while any exist. */
static struct { uint32_t dev, n; } live[16];
static void count_default(res *r, int delta)
{
    for (int i = 0; i < 16; i++) if (live[i].dev == r->device && (live[i].n || delta > 0)) { live[i].n += (uint32_t)delta; return; }
    for (int i = 0; i < 16; i++) if (!live[i].n && delta > 0) { live[i].dev = r->device; live[i].n = 1; return; }
    hp_unsupported("Direct3D 9", "default-pool resources on more than 16 devices");
}
static void track(res *r) { if (r->pool == 0 && !r->borrowed) { r->counted = 1; count_default(r, 1); } }
uint32_t halopad_d3d9_live_default(uint32_t dev)
{
    for (int i = 0; i < 16; i++) if (live[i].dev == dev) return live[i].n;
    return 0;
}

static void res_destroy(void *p)
{
    res *r = p;
    if (r->counted) count_default(r, -1);
    for (uint32_t l = 0; l < MAXLEVELS; l++) if (r->mem[l]) halopad_heap_free(r->mem[l]);
    if (r->buf) halopad_heap_free(r->buf);
    if (!r->borrowed) halopad_metal_release(r->native);
    halopad_metal_release(r->view);
    free(r->tokens);
    free(r);
}

static res *R(const char *iface, uint32_t g) { return halopad_com_state(iface, g); }

/* ---- IUnknown / IDirect3DResource9 / IDirect3DBaseTexture9, shared ---- */

static uint32_t c_qi(const char *i, uint32_t g, uint32_t iid, uint32_t out)
{
    (void)out; R(i, g);
    hp_unsupported(i, "QueryInterface for %08x-...", rd32(iid));
}
static uint32_t c_addref(const char *i, uint32_t g)
{
    res *r = R(i, g);
    return r->kind == R_SURFACE && r->parent ? halopad_com_addref(r->parent) : halopad_com_addref(g);
}
static uint32_t c_release(const char *i, uint32_t g)
{
    res *r = R(i, g);
    return r->kind == R_SURFACE && r->parent ? halopad_com_release(r->parent) : halopad_com_release(g);
}
uint32_t halopad_d3d9_addref(uint32_t g) { return c_addref(halopad_com_interface(g), g); }
static uint32_t c_getdevice(const char *i, uint32_t g, uint32_t out)
{
    res *r = R(i, g);
    halopad_com_addref(r->device);
    wr32(out, r->device);
    return D3D_OK;
}
static uint32_t c_privdata(const char *i, uint32_t g, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    (void)a; (void)b; (void)c; (void)d; R(i, g);
    hp_unsupported(i, "private data");
}
static uint32_t c_privdata1(const char *i, uint32_t g, uint32_t a) { return c_privdata(i, g, a, 0, 0, 0); }
static uint32_t c_privdata3(const char *i, uint32_t g, uint32_t a, uint32_t b, uint32_t c) { return c_privdata(i, g, a, b, c, 0); }
static uint32_t c_setpriority(const char *i, uint32_t g, uint32_t p)
{
    res *r = R(i, g);
    uint32_t old = r->priority;
    if (r->pool == 1) r->priority = p;                             /* managed resources only */
    return r->pool == 1 ? old : 0;
}
static uint32_t c_getpriority(const char *i, uint32_t g) { return R(i, g)->priority; }
static uint32_t c_preload(const char *i, uint32_t g) { R(i, g); return 0; }   /* a residency hint; nothing to do */
static uint32_t c_gettype(const char *i, uint32_t g)
{
    static const uint32_t t[] = {3, 1, 6, 7};
    res *r = R(i, g);
    if (r->kind == R_TEXTURE) return r->ttype == 3 ? 5u : r->ttype == 4 ? 4u : 3u;   /* CUBETEXTURE, VOLUMETEXTURE, TEXTURE */
    return t[r->kind];
}
static uint32_t c_setlod(const char *i, uint32_t g, uint32_t lod)
{
    res *r = R(i, g);
    uint32_t old = r->lod;
    if (r->pool != 1) return 0;
    r->lod = lod < r->levels ? lod : r->levels - 1;
    return old;
}
static uint32_t c_getlod(const char *i, uint32_t g) { return R(i, g)->lod; }
static uint32_t c_levelcount(const char *i, uint32_t g) { return R(i, g)->levels; }
static uint32_t c_setautogen(const char *i, uint32_t g, uint32_t f)
{
    res *r = R(i, g);
    if (!(r->usage & 0x400)) return D3DERR_INVALIDCALL;
    if (f != 1 && f != 2) return D3DERR_INVALIDCALL;                /* POINT, LINEAR */
    r->autogen_filter = f;
    return D3D_OK;
}
static uint32_t c_getautogen(const char *i, uint32_t g) { return R(i, g)->autogen_filter; }
static uint32_t c_genmips(const char *i, uint32_t g) { res *r = R(i, g); r->dirty[0] = 1; return 0; }

/* ---- shared methods per interface (wrappers generated from these lines) ---- */
HPCOM_FWD2(IDirect3DTexture9, QueryInterface, c_qi)
HPCOM_FWD0(IDirect3DTexture9, AddRef, c_addref)
HPCOM_FWD0(IDirect3DTexture9, Release, c_release)
HPCOM_FWD1(IDirect3DTexture9, GetDevice, c_getdevice)
HPCOM_FWD4(IDirect3DTexture9, SetPrivateData, c_privdata)
HPCOM_FWD3(IDirect3DTexture9, GetPrivateData, c_privdata3)
HPCOM_FWD1(IDirect3DTexture9, FreePrivateData, c_privdata1)
HPCOM_FWD1(IDirect3DTexture9, SetPriority, c_setpriority)
HPCOM_FWD0(IDirect3DTexture9, GetPriority, c_getpriority)
HPCOM_FWD0(IDirect3DTexture9, PreLoad, c_preload)
HPCOM_FWD0(IDirect3DTexture9, GetType, c_gettype)
HPCOM_FWD1(IDirect3DTexture9, SetLOD, c_setlod)
HPCOM_FWD0(IDirect3DTexture9, GetLOD, c_getlod)
HPCOM_FWD0(IDirect3DTexture9, GetLevelCount, c_levelcount)
HPCOM_FWD1(IDirect3DTexture9, SetAutoGenFilterType, c_setautogen)
HPCOM_FWD0(IDirect3DTexture9, GetAutoGenFilterType, c_getautogen)
HPCOM_FWD0(IDirect3DTexture9, GenerateMipSubLevels, c_genmips)
HPCOM_FWD2(IDirect3DCubeTexture9, QueryInterface, c_qi)
HPCOM_FWD0(IDirect3DCubeTexture9, AddRef, c_addref)
HPCOM_FWD0(IDirect3DCubeTexture9, Release, c_release)
HPCOM_FWD1(IDirect3DCubeTexture9, GetDevice, c_getdevice)
HPCOM_FWD4(IDirect3DCubeTexture9, SetPrivateData, c_privdata)
HPCOM_FWD3(IDirect3DCubeTexture9, GetPrivateData, c_privdata3)
HPCOM_FWD1(IDirect3DCubeTexture9, FreePrivateData, c_privdata1)
HPCOM_FWD1(IDirect3DCubeTexture9, SetPriority, c_setpriority)
HPCOM_FWD0(IDirect3DCubeTexture9, GetPriority, c_getpriority)
HPCOM_FWD0(IDirect3DCubeTexture9, PreLoad, c_preload)
HPCOM_FWD0(IDirect3DCubeTexture9, GetType, c_gettype)
HPCOM_FWD1(IDirect3DCubeTexture9, SetLOD, c_setlod)
HPCOM_FWD0(IDirect3DCubeTexture9, GetLOD, c_getlod)
HPCOM_FWD0(IDirect3DCubeTexture9, GetLevelCount, c_levelcount)
HPCOM_FWD1(IDirect3DCubeTexture9, SetAutoGenFilterType, c_setautogen)
HPCOM_FWD0(IDirect3DCubeTexture9, GetAutoGenFilterType, c_getautogen)
HPCOM_FWD0(IDirect3DCubeTexture9, GenerateMipSubLevels, c_genmips)
HPCOM_FWD2(IDirect3DVolumeTexture9, QueryInterface, c_qi)
HPCOM_FWD0(IDirect3DVolumeTexture9, AddRef, c_addref)
HPCOM_FWD0(IDirect3DVolumeTexture9, Release, c_release)
HPCOM_FWD1(IDirect3DVolumeTexture9, GetDevice, c_getdevice)
HPCOM_FWD4(IDirect3DVolumeTexture9, SetPrivateData, c_privdata)
HPCOM_FWD3(IDirect3DVolumeTexture9, GetPrivateData, c_privdata3)
HPCOM_FWD1(IDirect3DVolumeTexture9, FreePrivateData, c_privdata1)
HPCOM_FWD1(IDirect3DVolumeTexture9, SetPriority, c_setpriority)
HPCOM_FWD0(IDirect3DVolumeTexture9, GetPriority, c_getpriority)
HPCOM_FWD0(IDirect3DVolumeTexture9, PreLoad, c_preload)
HPCOM_FWD0(IDirect3DVolumeTexture9, GetType, c_gettype)
HPCOM_FWD1(IDirect3DVolumeTexture9, SetLOD, c_setlod)
HPCOM_FWD0(IDirect3DVolumeTexture9, GetLOD, c_getlod)
HPCOM_FWD0(IDirect3DVolumeTexture9, GetLevelCount, c_levelcount)
HPCOM_FWD1(IDirect3DVolumeTexture9, SetAutoGenFilterType, c_setautogen)
HPCOM_FWD0(IDirect3DVolumeTexture9, GetAutoGenFilterType, c_getautogen)
HPCOM_FWD0(IDirect3DVolumeTexture9, GenerateMipSubLevels, c_genmips)
HPCOM_FWD2(IDirect3DSurface9, QueryInterface, c_qi)
HPCOM_FWD0(IDirect3DSurface9, AddRef, c_addref)
HPCOM_FWD0(IDirect3DSurface9, Release, c_release)
HPCOM_FWD1(IDirect3DSurface9, GetDevice, c_getdevice)
HPCOM_FWD4(IDirect3DSurface9, SetPrivateData, c_privdata)
HPCOM_FWD3(IDirect3DSurface9, GetPrivateData, c_privdata3)
HPCOM_FWD1(IDirect3DSurface9, FreePrivateData, c_privdata1)
HPCOM_FWD1(IDirect3DSurface9, SetPriority, c_setpriority)
HPCOM_FWD0(IDirect3DSurface9, GetPriority, c_getpriority)
HPCOM_FWD0(IDirect3DSurface9, PreLoad, c_preload)
HPCOM_FWD0(IDirect3DSurface9, GetType, c_gettype)
HPCOM_FWD2(IDirect3DVertexBuffer9, QueryInterface, c_qi)
HPCOM_FWD0(IDirect3DVertexBuffer9, AddRef, c_addref)
HPCOM_FWD0(IDirect3DVertexBuffer9, Release, c_release)
HPCOM_FWD1(IDirect3DVertexBuffer9, GetDevice, c_getdevice)
HPCOM_FWD4(IDirect3DVertexBuffer9, SetPrivateData, c_privdata)
HPCOM_FWD3(IDirect3DVertexBuffer9, GetPrivateData, c_privdata3)
HPCOM_FWD1(IDirect3DVertexBuffer9, FreePrivateData, c_privdata1)
HPCOM_FWD1(IDirect3DVertexBuffer9, SetPriority, c_setpriority)
HPCOM_FWD0(IDirect3DVertexBuffer9, GetPriority, c_getpriority)
HPCOM_FWD0(IDirect3DVertexBuffer9, PreLoad, c_preload)
HPCOM_FWD0(IDirect3DVertexBuffer9, GetType, c_gettype)
HPCOM_FWD2(IDirect3DIndexBuffer9, QueryInterface, c_qi)
HPCOM_FWD0(IDirect3DIndexBuffer9, AddRef, c_addref)
HPCOM_FWD0(IDirect3DIndexBuffer9, Release, c_release)
HPCOM_FWD1(IDirect3DIndexBuffer9, GetDevice, c_getdevice)
HPCOM_FWD4(IDirect3DIndexBuffer9, SetPrivateData, c_privdata)
HPCOM_FWD3(IDirect3DIndexBuffer9, GetPrivateData, c_privdata3)
HPCOM_FWD1(IDirect3DIndexBuffer9, FreePrivateData, c_privdata1)
HPCOM_FWD1(IDirect3DIndexBuffer9, SetPriority, c_setpriority)
HPCOM_FWD0(IDirect3DIndexBuffer9, GetPriority, c_getpriority)
HPCOM_FWD0(IDirect3DIndexBuffer9, PreLoad, c_preload)
HPCOM_FWD0(IDirect3DIndexBuffer9, GetType, c_gettype)
HPCOM_FWD2(IDirect3DVertexDeclaration9, QueryInterface, c_qi)
HPCOM_FWD0(IDirect3DVertexDeclaration9, AddRef, c_addref)
HPCOM_FWD0(IDirect3DVertexDeclaration9, Release, c_release)
HPCOM_FWD1(IDirect3DVertexDeclaration9, GetDevice, c_getdevice)
HPCOM_FWD2(IDirect3DVertexShader9, QueryInterface, c_qi)
HPCOM_FWD0(IDirect3DVertexShader9, AddRef, c_addref)
HPCOM_FWD0(IDirect3DVertexShader9, Release, c_release)
HPCOM_FWD1(IDirect3DVertexShader9, GetDevice, c_getdevice)
HPCOM_FWD2(IDirect3DPixelShader9, QueryInterface, c_qi)
HPCOM_FWD0(IDirect3DPixelShader9, AddRef, c_addref)
HPCOM_FWD0(IDirect3DPixelShader9, Release, c_release)
HPCOM_FWD1(IDirect3DPixelShader9, GetDevice, c_getdevice)

/* ---- textures (2D, cube, volume) and their surfaces ----
 * Level l of a texture is one block of guest memory: a 2D image, six faces of size[l]
 * each (cube, in D3DCUBEMAP_FACES order), or ld[l] slices of slice[l] bytes (volume). */

static uint32_t new_surface(res *t, uint32_t face, uint32_t level);

static uint32_t level_offset(res *r, uint32_t l, uint32_t rect)
{
    if (!rect) return 0;
    uint32_t bytes, block;
    fmt(r->format, &bytes, &block);
    uint32_t left = rd32(rect), top = rd32(rect + 4), right = rd32(rect + 8), bottom = rd32(rect + 12);
    if (right <= left || bottom <= top || right > r->lw[l] || bottom > r->lh[l] || (block > 1 && ((left | top) & 3)))
        return 0xFFFFFFFFu;
    return (top / block) * r->pitch[l] + (left / block) * bytes;
}

static void check_lock_flags(uint32_t flags)
{
    if (flags & ~(0x10u | 0x800u | 0x1000u | 0x2000u | 0x4000u | 0x8000u)) hp_unsupported("LockRect", "flags 0x%x", flags);
}

static uint32_t lock_level(res *r, uint32_t face, uint32_t l, uint32_t out, uint32_t rect, uint32_t flags)
{
    if (l >= r->levels || (r->locked[l] >> face & 1)) return D3DERR_INVALIDCALL;
    check_lock_flags(flags);
    if (!r->mem[l] || r->no_lock) return D3DERR_INVALIDCALL;                      /* render targets, non-dynamic default pool */
    uint32_t off = level_offset(r, l, rect);
    if (off == 0xFFFFFFFFu) return D3DERR_INVALIDCALL;
    wr32(out, r->pitch[l]);
    wr32(out + 4, r->mem[l] + face * r->size[l] + off);
    r->locked[l] |= (uint8_t)(1u << face);
    if (!(flags & (0x10u | 0x8000u))) r->dirty[l] = 1;              /* not READONLY or NO_DIRTY_UPDATE */
    return D3D_OK;
}

static uint32_t unlock_level(res *r, uint32_t face, uint32_t l)
{
    if (l >= r->levels || !(r->locked[l] >> face & 1)) return D3DERR_INVALIDCALL;
    r->locked[l] &= (uint8_t)~(1u << face);
    return D3D_OK;
}

static void level_desc(res *r, uint32_t l, uint32_t d)
{
    wr32(d, r->format); wr32(d + 4, 1 /* D3DRTYPE_SURFACE */); wr32(d + 8, r->usage); wr32(d + 12, r->pool);
    wr32(d + 16, 0); wr32(d + 20, 0); wr32(d + 24, r->lw[l]); wr32(d + 28, r->lh[l]);
}

/* ttype: 2 = 2D, 3 = cube (w = h = edge), 4 = volume */
static uint32_t new_texture(const char *iface, uint32_t ttype, uint32_t dev, uint32_t w, uint32_t h, uint32_t depth, uint32_t levels,
                            uint32_t usage, uint32_t format, uint32_t pool, uint32_t out, uint32_t shared)
{
    halopad_com_state("IDirect3DDevice9", dev);
    uint32_t bytes, block;
    if (shared || !out) return D3DERR_INVALIDCALL;
    if (!w || !h || !depth || pool > 3) return D3DERR_INVALIDCALL;
    if (usage & ~(0x1u | 0x2u | 0x200u | 0x400u)) hp_unsupported(iface, "usage 0x%x", usage);
    if ((usage & 0x3) && pool != 0) return D3DERR_INVALIDCALL;       /* render/depth targets live in the default pool */
    if ((usage & 0x200) && pool == 1) return D3DERR_INVALIDCALL;     /* dynamic textures are not managed */
    if ((usage & 0x3) && ttype == 4) return D3DERR_INVALIDCALL;      /* volumes are never targets */
    if (usage & 0x2) hp_unsupported(iface, "depth-stencil textures");
    if ((usage & 0x1) && ttype == 3) hp_unsupported(iface, "render-target cube textures");
    if (!fmt(format, &bytes, &block)) hp_unsupported(iface, "format %u", format);
    if ((usage & 0x1) && format != 21 && format != 22 && format != 23) return D3DERR_INVALIDCALL;   /* the contract's render-target formats */
    if (w > 2048 || h > 2048 || depth > 2048) return D3DERR_INVALIDCALL;   /* MaxTextureWidth/Height, MaxVolumeExtent */
    uint32_t full = 1;
    while ((w >> full) || (h >> full) || (depth >> full)) full++;
    if (usage & 0x400) {                                            /* autogen: 0 or 1 levels, one visible level */
        if (levels > 1) return D3DERR_INVALIDCALL;
        levels = 1;
    } else if (!levels) {
        levels = full;
    }
    if (levels > full || levels > MAXLEVELS) return D3DERR_INVALIDCALL;
    res *r = calloc(1, sizeof *r);
    r->kind = R_TEXTURE; r->ttype = ttype; r->device = dev; r->usage = usage; r->format = format; r->pool = pool;
    r->width = w; r->height = h; r->depth = depth; r->levels = levels; r->autogen_filter = (usage & 0x400) ? 2 : 0;
    int lockable = !(usage & 0x3) && (pool != 0 || (usage & 0x200));
    r->no_lock = !lockable;
    for (uint32_t l = 0; l < levels; l++) {
        r->lw[l] = w >> l ? w >> l : 1;
        r->lh[l] = h >> l ? h >> l : 1;
        r->ld[l] = depth >> l ? depth >> l : 1;
        r->pitch[l] = ((r->lw[l] + block - 1) / block) * bytes;
        r->slice[l] = r->pitch[l] * ((r->lh[l] + block - 1) / block);
        r->size[l] = ttype == 4 ? r->slice[l] * r->ld[l] : r->slice[l];
        if (!(usage & 0x3)) r->mem[l] = halopad_heap_alloc(ttype == 3 ? 6 * r->size[l] : r->size[l], 1);   /* UpdateTexture fills default-pool ones */
        r->dirty[l] = 1;
    }
    track(r);
    r->guest = halopad_com_new(iface, 4, r, res_destroy);
    wr32(out, r->guest);
    return D3D_OK;
}

uint32_t hpcom_IDirect3DDevice9_CreateTexture_c(uint32_t dev, uint32_t w, uint32_t h, uint32_t levels, uint32_t usage,
                                                uint32_t format, uint32_t pool, uint32_t out, uint32_t shared)
{
    return new_texture("IDirect3DTexture9", 2, dev, w, h, 1, levels, usage, format, pool, out, shared);
}
uint32_t hpcom_IDirect3DDevice9_CreateCubeTexture_c(uint32_t dev, uint32_t edge, uint32_t levels, uint32_t usage, uint32_t format,
                                                    uint32_t pool, uint32_t out, uint32_t shared)
{
    return new_texture("IDirect3DCubeTexture9", 3, dev, edge, edge, 1, levels, usage, format, pool, out, shared);
}
uint32_t hpcom_IDirect3DDevice9_CreateVolumeTexture_c(uint32_t dev, uint32_t w, uint32_t h, uint32_t depth, uint32_t levels,
                                                      uint32_t usage, uint32_t format, uint32_t pool, uint32_t out, uint32_t shared)
{
    return new_texture("IDirect3DVolumeTexture9", 4, dev, w, h, depth, levels, usage, format, pool, out, shared);
}

uint32_t hpcom_IDirect3DTexture9_GetLevelDesc_c(uint32_t g, uint32_t l, uint32_t d)
{
    res *r = R("IDirect3DTexture9", g);
    if (l >= r->levels) return D3DERR_INVALIDCALL;
    level_desc(r, l, d);
    return D3D_OK;
}
static uint32_t surface_of(res *r, uint32_t face, uint32_t l, uint32_t out)
{
    uint32_t *slot = &r->surface[face * MAXLEVELS + l];
    if (!*slot) *slot = new_surface(r, face, l);
    halopad_com_addref(r->guest);                                   /* the surface shares the texture's count */
    wr32(out, *slot);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DTexture9_GetSurfaceLevel_c(uint32_t g, uint32_t l, uint32_t out)
{
    res *r = R("IDirect3DTexture9", g);
    if (l >= r->levels || !out) return D3DERR_INVALIDCALL;
    return surface_of(r, 0, l, out);
}
uint32_t hpcom_IDirect3DTexture9_LockRect_c(uint32_t g, uint32_t l, uint32_t out, uint32_t rect, uint32_t flags)
{
    return lock_level(R("IDirect3DTexture9", g), 0, l, out, rect, flags);
}
uint32_t hpcom_IDirect3DTexture9_UnlockRect_c(uint32_t g, uint32_t l) { return unlock_level(R("IDirect3DTexture9", g), 0, l); }
uint32_t hpcom_IDirect3DTexture9_AddDirtyRect_c(uint32_t g, uint32_t rect)
{
    (void)rect;
    res *r = R("IDirect3DTexture9", g);
    r->dirty[0] = 1;
    return D3D_OK;
}

uint32_t hpcom_IDirect3DCubeTexture9_GetLevelDesc_c(uint32_t g, uint32_t l, uint32_t d)
{
    res *r = R("IDirect3DCubeTexture9", g);
    if (l >= r->levels) return D3DERR_INVALIDCALL;
    level_desc(r, l, d);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DCubeTexture9_GetCubeMapSurface_c(uint32_t g, uint32_t face, uint32_t l, uint32_t out)
{
    res *r = R("IDirect3DCubeTexture9", g);
    if (face > 5 || l >= r->levels || !out) return D3DERR_INVALIDCALL;
    return surface_of(r, face, l, out);
}
uint32_t hpcom_IDirect3DCubeTexture9_LockRect_c(uint32_t g, uint32_t face, uint32_t l, uint32_t out, uint32_t rect, uint32_t flags)
{
    if (face > 5) return D3DERR_INVALIDCALL;
    return lock_level(R("IDirect3DCubeTexture9", g), face, l, out, rect, flags);
}
uint32_t hpcom_IDirect3DCubeTexture9_UnlockRect_c(uint32_t g, uint32_t face, uint32_t l)
{
    if (face > 5) return D3DERR_INVALIDCALL;
    return unlock_level(R("IDirect3DCubeTexture9", g), face, l);
}
uint32_t hpcom_IDirect3DCubeTexture9_AddDirtyRect_c(uint32_t g, uint32_t face, uint32_t rect)
{
    (void)rect;
    res *r = R("IDirect3DCubeTexture9", g);
    if (face > 5) return D3DERR_INVALIDCALL;
    r->dirty[0] = 1;
    return D3D_OK;
}

uint32_t hpcom_IDirect3DVolumeTexture9_GetLevelDesc_c(uint32_t g, uint32_t l, uint32_t d)
{
    res *r = R("IDirect3DVolumeTexture9", g);
    if (l >= r->levels) return D3DERR_INVALIDCALL;
    wr32(d, r->format); wr32(d + 4, 2 /* D3DRTYPE_VOLUME */); wr32(d + 8, r->usage); wr32(d + 12, r->pool);
    wr32(d + 16, r->lw[l]); wr32(d + 20, r->lh[l]); wr32(d + 24, r->ld[l]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DVolumeTexture9_GetVolumeLevel_c(uint32_t g, uint32_t l, uint32_t out)
{
    (void)l; (void)out; R("IDirect3DVolumeTexture9", g);
    hp_unsupported("IDirect3DVolumeTexture9::GetVolumeLevel", "IDirect3DVolume9 objects");
}
uint32_t hpcom_IDirect3DVolumeTexture9_LockBox_c(uint32_t g, uint32_t l, uint32_t out, uint32_t box, uint32_t flags)
{
    res *r = R("IDirect3DVolumeTexture9", g);
    if (l >= r->levels || r->locked[l] || !out) return D3DERR_INVALIDCALL;
    check_lock_flags(flags);
    if (!r->mem[l] || r->no_lock) return D3DERR_INVALIDCALL;
    uint32_t off = 0;
    if (box) {
        uint32_t bytes, block;
        fmt(r->format, &bytes, &block);
        uint32_t left = rd32(box), top = rd32(box + 4), right = rd32(box + 8), bottom = rd32(box + 12);
        uint32_t front = rd32(box + 16), back = rd32(box + 20);
        if (right <= left || bottom <= top || back <= front || right > r->lw[l] || bottom > r->lh[l] || back > r->ld[l]
            || (block > 1 && ((left | top) & 3)))
            return D3DERR_INVALIDCALL;
        off = front * r->slice[l] + (top / block) * r->pitch[l] + (left / block) * bytes;
    }
    wr32(out, r->pitch[l]); wr32(out + 4, r->slice[l]); wr32(out + 8, r->mem[l] + off);
    r->locked[l] = 1;
    if (!(flags & (0x10u | 0x8000u))) r->dirty[l] = 1;
    return D3D_OK;
}
uint32_t hpcom_IDirect3DVolumeTexture9_UnlockBox_c(uint32_t g, uint32_t l) { return unlock_level(R("IDirect3DVolumeTexture9", g), 0, l); }
uint32_t hpcom_IDirect3DVolumeTexture9_AddDirtyBox_c(uint32_t g, uint32_t box)
{
    (void)box;
    res *r = R("IDirect3DVolumeTexture9", g);
    r->dirty[0] = 1;
    return D3D_OK;
}

static uint32_t new_surface(res *t, uint32_t face, uint32_t level)
{
    res *s = calloc(1, sizeof *s);
    s->kind = R_SURFACE; s->device = t->device; s->parent = t->guest; s->face = face; s->level = level;
    s->format = t->format; s->usage = t->usage; s->pool = t->pool; s->levels = 1;
    s->lw[0] = t->lw[level]; s->lh[0] = t->lh[level];
    return halopad_com_new("IDirect3DSurface9", 4, s, res_destroy);
}

/* A stand-alone surface: the back buffer and automatic depth/stencil (borrowed: the Metal
   target's textures) or an offscreen plain surface (lockable guest memory, uploaded to
   Metal when a copy reads it). */
uint32_t halopad_d3d9_surface_new(uint32_t dev, uint32_t w, uint32_t h, uint32_t format, uint32_t usage, uint32_t pool,
                                  void *borrowed, int lockable)
{
    uint32_t bytes = 4, block = 1;
    res *s = calloc(1, sizeof *s);
    s->kind = R_SURFACE; s->device = dev; s->format = format; s->usage = usage; s->pool = pool; s->levels = 1;
    s->width = w; s->height = h; s->lw[0] = w; s->lh[0] = h;
    if (borrowed) { s->native = borrowed; s->borrowed = 1; }
    if (lockable) {
        fmt(format, &bytes, &block);
        s->pitch[0] = ((w + block - 1) / block) * bytes;
        s->size[0] = s->pitch[0] * ((h + block - 1) / block);
        s->mem[0] = halopad_heap_alloc(s->size[0], 1);
        s->dirty[0] = 1;
    }
    track(s);
    s->guest = halopad_com_new("IDirect3DSurface9", 4, s, res_destroy);
    return s->guest;
}

uint32_t hpcom_IDirect3DDevice9_CreateOffscreenPlainSurface_c(uint32_t dev, uint32_t w, uint32_t h, uint32_t format, uint32_t pool,
                                                              uint32_t out, uint32_t shared)
{
    halopad_com_state("IDirect3DDevice9", dev);
    uint32_t bytes, block;
    if (shared || !w || !h || !out || pool == 1 || pool > 3) return D3DERR_INVALIDCALL;   /* not MANAGED */
    if (!fmt(format, &bytes, &block)) return D3DERR_INVALIDCALL;
    if (block > 1 && ((w | h) & 3)) return D3DERR_INVALIDCALL;
    wr32(out, halopad_d3d9_surface_new(dev, w, h, format, 0, pool, NULL, 1));
    return D3D_OK;
}

static res *texture_of(res *s) { return R(halopad_com_interface(s->parent), s->parent); }

uint32_t hpcom_IDirect3DSurface9_GetContainer_c(uint32_t g, uint32_t iid, uint32_t out)
{
    (void)iid;
    res *s = R("IDirect3DSurface9", g);
    if (!s->parent) hp_unsupported("IDirect3DSurface9::GetContainer", "a surface without a texture");
    halopad_com_addref(s->parent);
    wr32(out, s->parent);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DSurface9_GetDesc_c(uint32_t g, uint32_t d)
{
    res *s = R("IDirect3DSurface9", g);
    if (s->parent) level_desc(texture_of(s), s->level, d);
    else level_desc(s, 0, d);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DSurface9_LockRect_c(uint32_t g, uint32_t out, uint32_t rect, uint32_t flags)
{
    res *s = R("IDirect3DSurface9", g);
    if (!s->parent) return lock_level(s, 0, 0, out, rect, flags);     /* back buffer and depth: no memory, not lockable */
    return lock_level(texture_of(s), s->face, s->level, out, rect, flags);
}
uint32_t hpcom_IDirect3DSurface9_UnlockRect_c(uint32_t g)
{
    res *s = R("IDirect3DSurface9", g);
    if (!s->parent) return unlock_level(s, 0, 0);
    return unlock_level(texture_of(s), s->face, s->level);
}

/* ---- vertex and index buffers ---- */

static uint32_t new_buffer(const char *iface, int kind, uint32_t dev, uint32_t length, uint32_t usage, uint32_t fvf_or_fmt,
                           uint32_t pool, uint32_t out, uint32_t shared)
{
    halopad_com_state("IDirect3DDevice9", dev);
    if (shared || !length || pool > 2) return D3DERR_INVALIDCALL;
    if (usage & ~(0x8u | 0x10u | 0x20u | 0x200u)) hp_unsupported(iface, "usage 0x%x", usage);   /* WRITEONLY, SOFTWAREPROCESSING, DONOTCLIP, DYNAMIC */
    if ((usage & 0x200) && pool == 1) return D3DERR_INVALIDCALL;
    if (kind == R_IB && fvf_or_fmt != 101 && fvf_or_fmt != 102) return D3DERR_INVALIDCALL;
    res *r = calloc(1, sizeof *r);
    r->kind = kind; r->device = dev; r->length = length; r->usage = usage; r->pool = pool;
    if (kind == R_VB) r->fvf = fvf_or_fmt; else r->format = fvf_or_fmt;
    r->buf = halopad_heap_alloc(length, 1);
    r->dirty[0] = 1;
    track(r);
    r->guest = halopad_com_new(iface, 4, r, res_destroy);
    wr32(out, r->guest);
    return D3D_OK;
}

uint32_t hpcom_IDirect3DDevice9_CreateVertexBuffer_c(uint32_t dev, uint32_t length, uint32_t usage, uint32_t fvf, uint32_t pool,
                                                     uint32_t out, uint32_t shared)
{
    return new_buffer("IDirect3DVertexBuffer9", R_VB, dev, length, usage, fvf, pool, out, shared);
}
uint32_t hpcom_IDirect3DDevice9_CreateIndexBuffer_c(uint32_t dev, uint32_t length, uint32_t usage, uint32_t format, uint32_t pool,
                                                    uint32_t out, uint32_t shared)
{
    return new_buffer("IDirect3DIndexBuffer9", R_IB, dev, length, usage, format, pool, out, shared);
}

static uint32_t buffer_lock(const char *i, uint32_t g, uint32_t offset, uint32_t size, uint32_t out, uint32_t flags)
{
    res *r = R(i, g);
    if (flags & ~(0x10u | 0x800u | 0x1000u | 0x2000u | 0x4000u)) hp_unsupported(i, "Lock flags 0x%x", flags);
    if (!size) size = r->length - (offset < r->length ? offset : r->length);
    if (offset > r->length || size > r->length - offset) return D3DERR_INVALIDCALL;
    wr32(out, r->buf + offset);
    r->locks++;
    if (!(flags & 0x10u)) r->dirty[0] = 1;
    return D3D_OK;
}
static uint32_t buffer_unlock(const char *i, uint32_t g)
{
    res *r = R(i, g);
    if (!r->locks) return D3DERR_INVALIDCALL;
    r->locks--;
    return D3D_OK;
}
uint32_t hpcom_IDirect3DVertexBuffer9_Lock_c(uint32_t g, uint32_t o, uint32_t s, uint32_t out, uint32_t f)
{
    return buffer_lock("IDirect3DVertexBuffer9", g, o, s, out, f);
}
uint32_t hpcom_IDirect3DVertexBuffer9_Unlock_c(uint32_t g) { return buffer_unlock("IDirect3DVertexBuffer9", g); }
uint32_t hpcom_IDirect3DVertexBuffer9_GetDesc_c(uint32_t g, uint32_t d)
{
    res *r = R("IDirect3DVertexBuffer9", g);
    wr32(d, 100); wr32(d + 4, 6); wr32(d + 8, r->usage); wr32(d + 12, r->pool); wr32(d + 16, r->length); wr32(d + 20, r->fvf);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DIndexBuffer9_Lock_c(uint32_t g, uint32_t o, uint32_t s, uint32_t out, uint32_t f)
{
    return buffer_lock("IDirect3DIndexBuffer9", g, o, s, out, f);
}
uint32_t hpcom_IDirect3DIndexBuffer9_Unlock_c(uint32_t g) { return buffer_unlock("IDirect3DIndexBuffer9", g); }
uint32_t hpcom_IDirect3DIndexBuffer9_GetDesc_c(uint32_t g, uint32_t d)
{
    res *r = R("IDirect3DIndexBuffer9", g);
    wr32(d, r->format); wr32(d + 4, 7); wr32(d + 8, r->usage); wr32(d + 12, r->pool); wr32(d + 16, r->length);
    return D3D_OK;
}

/* ---- vertex declarations ---- */

uint32_t hpcom_IDirect3DDevice9_CreateVertexDeclaration_c(uint32_t dev, uint32_t elements, uint32_t out)
{
    halopad_com_state("IDirect3DDevice9", dev);
    uint32_t n = 0;
    for (;; n++) {
        if (n == 65) return D3DERR_INVALIDCALL;
        uint32_t e = elements + 8 * n;
        uint16_t stream = (uint16_t)(rd32(e) & 0xFFFF);
        uint8_t type = ((uint8_t *)G(e))[4], method = ((uint8_t *)G(e))[5], usage = ((uint8_t *)G(e))[6];
        if (stream == 0xFF) { if (type != 17) return D3DERR_INVALIDCALL; break; }   /* D3DDECL_END */
        if (stream >= 16 || type > 16 || method > 6 || usage > 13) return D3DERR_INVALIDCALL;
    }
    res *r = calloc(1, sizeof *r);
    r->kind = R_DECL; r->device = dev; r->count = n + 1;
    r->tokens = malloc(8 * r->count);
    memcpy(r->tokens, G(elements), 8 * r->count);
    r->guest = halopad_com_new("IDirect3DVertexDeclaration9", 4, r, res_destroy);
    wr32(out, r->guest);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DVertexDeclaration9_GetDeclaration_c(uint32_t g, uint32_t elements, uint32_t count)
{
    res *r = R("IDirect3DVertexDeclaration9", g);
    if (elements) memcpy(G(elements), r->tokens, 8 * r->count);
    wr32(count, r->count);
    return D3D_OK;
}

/* ---- shaders ---- */

/* Token count of a shader including its END token, or 0 if malformed. Instruction tokens
   have bit 31 clear; parameter tokens have it set. Comments carry their length; from
   version 2.0 every instruction carries its length; in 1.x only 'def' (opcode 0x51) has
   raw (non-parameter) operands. */
static uint32_t shader_length(uint32_t addr, uint32_t major_min, uint32_t prefix)
{
    uint32_t version = rd32(addr);
    if ((version >> 16) != prefix) return 0;
    uint32_t major = (version >> 8) & 0xFF;
    if (major < major_min) return 0;
    for (uint32_t i = 1; i < 1u << 16;) {
        uint32_t t = rd32(addr + 4 * i);
        if (t == 0x0000FFFFu) return i + 1;
        if (t & 0x80000000u) return 0;
        uint32_t op = t & 0xFFFF;
        if (op == 0xFFFE) { i += 1 + ((t >> 16) & 0x7FFF); continue; }
        if (major >= 2) { i += 1 + ((t >> 24) & 0xF); continue; }
        if (op == 0x51) { i += 6; continue; }
        i++;
        while (rd32(addr + 4 * i) & 0x80000000u) i++;
    }
    return 0;
}

static void dump_shader(const char *kind, const uint32_t *tokens, uint32_t n)
{
    const char *dir = getenv("HALOPAD_SHADER_DUMP");
    if (!dir) return;
    unsigned char md[32];
    CC_SHA256(tokens, (CC_LONG)(4 * n), md);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s_%02x%02x%02x%02x%02x%02x.bin", dir, kind, md[0], md[1], md[2], md[3], md[4], md[5]);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(tokens, 4, n, f); fclose(f); }
}

static uint32_t new_shader(const char *iface, int kind, uint32_t dev, uint32_t fn, uint32_t out)
{
    halopad_com_state("IDirect3DDevice9", dev);
    uint32_t v = rd32(fn);
    int ok = kind == R_VS ? (v == 0xFFFE0101u || v == 0xFFFE0200u)
                          : (v == 0xFFFF0101u || v == 0xFFFF0102u || v == 0xFFFF0103u || v == 0xFFFF0104u || v == 0xFFFF0200u);
    if (!ok) return D3DERR_INVALIDCALL;
    uint32_t n = shader_length(fn, 1, kind == R_VS ? 0xFFFE : 0xFFFF);
    if (!n) return D3DERR_INVALIDCALL;
    res *r = calloc(1, sizeof *r);
    r->kind = kind; r->device = dev; r->count = n;
    r->tokens = malloc(4 * n);
    memcpy(r->tokens, G(fn), 4 * n);
    dump_shader(kind == R_VS ? "vs" : "ps", r->tokens, n);
    r->guest = halopad_com_new(iface, 4, r, res_destroy);
    wr32(out, r->guest);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_CreateVertexShader_c(uint32_t dev, uint32_t fn, uint32_t out)
{
    return new_shader("IDirect3DVertexShader9", R_VS, dev, fn, out);
}
uint32_t hpcom_IDirect3DDevice9_CreatePixelShader_c(uint32_t dev, uint32_t fn, uint32_t out)
{
    return new_shader("IDirect3DPixelShader9", R_PS, dev, fn, out);
}
static uint32_t shader_function(const char *i, uint32_t g, uint32_t data, uint32_t size)
{
    res *r = R(i, g);
    if (data) {
        if (rd32(size) < 4 * r->count) return D3DERR_INVALIDCALL;
        memcpy(G(data), r->tokens, 4 * r->count);
    }
    wr32(size, 4 * r->count);
    return D3D_OK;
}
HPCOM_FWD2(IDirect3DVertexShader9, GetFunction, shader_function)
HPCOM_FWD2(IDirect3DPixelShader9, GetFunction, shader_function)

/* Resource kind and state for the device's binding and draw code. */
void *halopad_d3d9_resource(uint32_t g, const char *iface) { return g ? halopad_com_state(iface, g) : NULL; }

/* UpdateTexture: a system-memory texture into a default-pool one of the same type and format.
   The destination's top level matches a source level by size, and the destination's levels are
   copied from that level down. Direct3D copies the dirty region, and the top level's region
   covers every sublevel. Here a level is copied whole when the source's top level or that level
   is dirty (locked without D3DLOCK_NO_DIRTY_UPDATE, AddDirtyRect, or new), and the source is then
   clean. Autogenerated destinations and render targets stop. */
uint32_t hpcom_IDirect3DDevice9_UpdateTexture_c(uint32_t dev, uint32_t src, uint32_t dst)
{
    halopad_com_state("IDirect3DDevice9", dev);
    if (!src || !dst) return D3DERR_INVALIDCALL;
    const char *is = halopad_com_interface(src), *id = halopad_com_interface(dst);
    if (strcmp(is, id) || (strcmp(is, "IDirect3DTexture9") && strcmp(is, "IDirect3DCubeTexture9") && strcmp(is, "IDirect3DVolumeTexture9")))
        return D3DERR_INVALIDCALL;
    res *s = R(is, src), *d = R(id, dst);
    if (s->pool != 2 || d->pool != 0 || s->format != d->format) return D3DERR_INVALIDCALL;   /* SYSTEMMEM into DEFAULT */
    if (d->usage & 0x1) hp_unsupported("UpdateTexture", "a render-target destination");
    if (d->usage & 0x400) hp_unsupported("UpdateTexture", "an autogenerated-mipmap destination");
    uint32_t s0 = 0;
    while (s0 < s->levels && !(s->lw[s0] == d->width && s->lh[s0] == d->height && s->ld[s0] == d->depth)) s0++;
    if (s0 == s->levels || s->levels - s0 < d->levels) return D3DERR_INVALIDCALL;
    uint32_t faces = s->ttype == 3 ? 6 : 1;
    for (uint32_t l = 0; l < d->levels; l++) {
        if (!s->dirty[0] && !s->dirty[s0 + l]) continue;
        if (s->size[s0 + l] != d->size[l]) hp_unsupported("UpdateTexture", "levels whose pitches differ");
        memmove(G(d->mem[l]), G(s->mem[s0 + l]), (size_t)faces * d->size[l]);
        d->dirty[l] = 1;                                           /* upload to Metal at the next draw */
    }
    for (uint32_t l = 0; l < s->levels; l++) s->dirty[l] = 0;
    return D3D_OK;
}
