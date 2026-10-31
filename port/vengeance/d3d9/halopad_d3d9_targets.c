/* HaloPad Direct3D 9, part 5 (G4): render targets, depth/stencil surfaces and StretchRect.
 *
 * The device's colour target is a surface: the back buffer (the Metal target's texture), or
 * a level of a render-target texture (a Metal texture Metal renders into and shaders sample).
 * Halo switches between the back buffer and its own render-target textures with
 * SetRenderTarget(0, ...), and copies with StretchRect: a 640x480 offscreen plain surface it
 * fills for the loading screen onto the render target, and render targets onto each other.
 * One simultaneous render target (the contract's NumSimultaneousRTs). X8R8G8B8 targets
 * keep alpha at 1 (clears write 1, draws mask alpha) so blending reads it as Direct3D does. */
#include "halopad_win32.h"

void *halopad_com_state(const char *iface, uint32_t g);
const char *halopad_com_interface(uint32_t g);
uint32_t halopad_com_release(uint32_t g);
void halopad_com_bind(uint32_t g);
void halopad_com_unbind(uint32_t g);
void *halopad_metal_target_back(void *target);
void *halopad_metal_target_depth(void *target);
void halopad_metal_set_attachments(void *target, void *color, uint32_t level, void *depth);
void *halopad_metal_render_texture(uint32_t format, uint32_t w, uint32_t h, uint32_t levels);
void *halopad_metal_texture_view(void *tex, const uint8_t swizzle[4]);
void halopad_metal_stretch(void *target, void *src, uint32_t slevel, const uint32_t srect[4], void *dst, uint32_t dlevel,
                           const uint32_t drect[4], int linear, int opaque);

#define D3D_OK 0u
#define D3DERR_INVALIDCALL 0x8876086Cu
#define D3DERR_NOTFOUND 0x88760866u
#include "halopad_d3d9_internal.h"

static device *dev(uint32_t g) { return halopad_com_state("IDirect3DDevice9", g); }
static int is_surface(uint32_t g) { return g && !strcmp(halopad_com_interface(g), "IDirect3DSurface9"); }
static res *S(uint32_t g) { return halopad_com_state("IDirect3DSurface9", g); }
/* A bound surface of a texture keeps the texture alive too, as Direct3D's reference does. */
static void rebind(uint32_t *slot, uint32_t g)
{
    if (*slot == g) return;
    if (g) {
        halopad_com_bind(g);
        if (S(g)->parent) halopad_com_bind(S(g)->parent);
    }
    if (*slot) {
        uint32_t parent = S(*slot)->parent;
        halopad_com_unbind(*slot);
        if (parent) halopad_com_unbind(parent);
    }
    *slot = g;
}

static uint32_t rt_mtl(uint32_t f) { return f == 23 ? 40u /* B5G6R5Unorm */ : 80u /* BGRA8Unorm */; }

static void *rt_texture(res *t)
{
    if (!t->native) {
        t->native = halopad_metal_render_texture(rt_mtl(t->format), t->width, t->height, t->levels);
        if (t->format == 22) { static const uint8_t sw[4] = {2, 3, 4, 1}; t->view = halopad_metal_texture_view(t->native, sw); }
    }
    return t->native;
}
void *halopad_d3d9_rt_view(res *t) { rt_texture(t); return t->view ? t->view : t->native; }

/* what a surface is on Metal: texture and level, size, format; rt: rendered by the GPU */
typedef struct { void *tex; uint32_t level, w, h, format; int rt; } surf;
static surf surface(res *s)
{
    surf o = {0};
    if (s->parent) {
        res *t = halopad_com_state(halopad_com_interface(s->parent), s->parent);
        if (t->ttype != 2) hp_unsupported("render target", "a %s surface", halopad_com_interface(s->parent));
        o.w = t->lw[s->level]; o.h = t->lh[s->level]; o.format = t->format; o.level = s->level;
        if (t->usage & 0x1) { o.tex = rt_texture(t); o.rt = 1; }
        else o.tex = halopad_d3d9_upload_texture(t);
    } else {
        o.w = s->width; o.h = s->height; o.format = s->format;
        if (s->mem[0]) o.tex = halopad_d3d9_upload_texture(s);
        else { o.tex = s->native; o.rt = 1; }
    }
    return o;
}

hp_bound halopad_d3d9_bind_targets(device *d)
{
    surf c = surface(S(d->rt));
    void *z = d->ds ? S(d->ds)->native : NULL;
    halopad_metal_set_attachments(d->target, c.tex, c.level, z);
    return (hp_bound){c.w, c.h, c.format, rt_mtl(c.format), z != NULL};
}

void halopad_d3d9_create_targets(device *d)
{
    d->backbuffer = halopad_d3d9_surface_new(d->guest, d->pp[0], d->pp[1], d->pp[2], 0x1, 0, halopad_metal_target_back(d->target), 0);
    rebind(&d->rt, d->backbuffer);
    if (d->pp[9]) {
        d->autods = halopad_d3d9_surface_new(d->guest, d->pp[0], d->pp[1], d->pp[10], 0x2, 0, halopad_metal_target_depth(d->target), 0);
        rebind(&d->ds, d->autods);
    }
}

void halopad_d3d9_release_targets(device *d)
{
    rebind(&d->rt, 0);
    rebind(&d->ds, 0);
    if (d->backbuffer) halopad_com_release(d->backbuffer);
    if (d->autods) halopad_com_release(d->autods);
    d->backbuffer = d->autods = 0;
}

uint32_t hpcom_IDirect3DDevice9_GetBackBuffer_c(uint32_t g, uint32_t swap, uint32_t index, uint32_t type, uint32_t out)
{
    device *d = dev(g);
    if (swap || index || type || !out) return D3DERR_INVALIDCALL;  /* one swap chain, one back buffer, MONO */
    halopad_d3d9_addref(d->backbuffer);
    wr32(out, d->backbuffer);
    return D3D_OK;
}

uint32_t hpcom_IDirect3DDevice9_GetRenderTarget_c(uint32_t g, uint32_t index, uint32_t out)
{
    device *d = dev(g);
    if (index || !out) return D3DERR_INVALIDCALL;
    if (!d->rt) return D3DERR_NOTFOUND;
    halopad_d3d9_addref(d->rt);
    wr32(out, d->rt);
    return D3D_OK;
}

int halopad_d3d9_tracing(void);
extern uint32_t halopad_d3d9_frame;
uint32_t hpcom_IDirect3DDevice9_SetRenderTarget_c(uint32_t g, uint32_t index, uint32_t s)
{
    if (halopad_d3d9_tracing()) fprintf(stderr, "HALOPAD DRAW f%u set render target %u = %08x\n", halopad_d3d9_frame, index, s);
    device *d = dev(g);
    if (index || !is_surface(s) || !(S(s)->usage & 0x1)) return D3DERR_INVALIDCALL;
    rebind(&d->rt, s);
    surf c = surface(S(s));
    /* Direct3D 9 resets the viewport to the whole new target */
    float one = 1.0f;
    d->viewport[0] = 0; d->viewport[1] = 0; d->viewport[2] = c.w; d->viewport[3] = c.h; d->viewport[4] = 0;
    memcpy(&d->viewport[5], &one, 4);
    return D3D_OK;
}

uint32_t hpcom_IDirect3DDevice9_GetDepthStencilSurface_c(uint32_t g, uint32_t out)
{
    device *d = dev(g);
    if (!out) return D3DERR_INVALIDCALL;
    if (!d->ds) return D3DERR_NOTFOUND;
    halopad_d3d9_addref(d->ds);
    wr32(out, d->ds);
    return D3D_OK;
}

uint32_t hpcom_IDirect3DDevice9_SetDepthStencilSurface_c(uint32_t g, uint32_t s)
{
    device *d = dev(g);
    if (s && (!is_surface(s) || !(S(s)->usage & 0x2))) return D3DERR_INVALIDCALL;
    rebind(&d->ds, s);
    return D3D_OK;
}

/* a RECT or the whole surface as x, y, w, h; 0 if outside */
static int rect(uint32_t r, uint32_t w, uint32_t h, uint32_t out[4])
{
    if (!r) { out[0] = 0; out[1] = 0; out[2] = w; out[3] = h; return 1; }
    int32_t l = (int32_t)rd32(r), t = (int32_t)rd32(r + 4), rr = (int32_t)rd32(r + 8), b = (int32_t)rd32(r + 12);
    if (l < 0 || t < 0 || rr <= l || b <= t || (uint32_t)rr > w || (uint32_t)b > h) return 0;
    out[0] = (uint32_t)l; out[1] = (uint32_t)t; out[2] = (uint32_t)(rr - l); out[3] = (uint32_t)(b - t);
    return 1;
}

uint32_t hpcom_IDirect3DDevice9_StretchRect_c(uint32_t g, uint32_t src, uint32_t sr, uint32_t dst, uint32_t dr, uint32_t filter)
{
    if (halopad_d3d9_tracing()) fprintf(stderr, "HALOPAD DRAW f%u stretch %08x -> %08x filter %u\n", halopad_d3d9_frame, src, dst, filter);
    device *d = dev(g);
    if (!is_surface(src) || !is_surface(dst) || src == dst || filter > 2) return D3DERR_INVALIDCALL;   /* NONE, POINT, LINEAR */
    res *s = S(src), *t = S(dst);
    if (s->pool != 0 || t->pool != 0) return D3DERR_INVALIDCALL;   /* both in the default pool */
    if ((s->usage | t->usage) & 0x2) hp_unsupported("IDirect3DDevice9::StretchRect", "depth/stencil surfaces");
    if (!(t->usage & 0x1)) hp_unsupported("IDirect3DDevice9::StretchRect", "into a surface that is not a render target");
    uint32_t bytes, block;
    if (!halopad_d3d9_format_size(s->format, &bytes, &block) || block > 1) return D3DERR_INVALIDCALL;
    if (s->parent && !(s->usage & 0x1)) return D3DERR_INVALIDCALL; /* texture sources must be render targets */
    surf a = surface(s), b = surface(t);
    uint32_t ra[4], rb[4];
    if (!rect(sr, a.w, a.h, ra) || !rect(dr, b.w, b.h, rb)) return D3DERR_INVALIDCALL;
    /* alpha 1 where Direct3D reads it so: X8 destinations, and X8 sources whose X byte is Halo's */
    int opaque = (a.format == 22 && !a.rt) || (b.format == 22 && a.format != 22);
    halopad_metal_stretch(d->target, a.tex, a.level, ra, b.tex, b.level, rb, filter == 2, opaque);
    return D3D_OK;
}
