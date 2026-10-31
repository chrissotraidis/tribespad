/* HaloPad Direct3D 9, part 2 (G3/G4): IDirect3DDevice9 on Metal.
 *
 * Implemented so far: device creation on Halo's window, reference counting, cooperative
 * level, caps/display/creation queries, scenes, Clear (whole targets) and Present (whole
 * back buffer), and the full fixed state: render states, texture-stage states, sampler
 * states, transforms and viewport, starting from Direct3D 9's documented defaults.
 * Resources, draws, fixed function and render targets are in the other halopad_d3d9_*.c files.
 * Every other method traps with its name (generated stubs). Unknown state numbers,
 * partial clears/presents, multisampling and pure devices stop with their values. */
#include "halopad_win32.h"
#ifndef PTROFS_64BIT
#define PTROFS_64BIT 1
#endif
#include "llasm_cpu.h"

uint32_t halopad_com_new(const char *iface, uint32_t size, void *state, void (*destroy)(void *));
uint32_t halopad_com_addref(uint32_t g);
uint32_t halopad_com_release(uint32_t g);
void *halopad_com_state(const char *iface, uint32_t g);
void halopad_d3d9_fill_caps(uint32_t c);
void halopad_desktop_size(int32_t *w, int32_t *h);
int halopad_window_client_size(uint32_t hwnd, int32_t *w, int32_t *h);
const char *halopad_window_text(uint32_t hwnd);
void *halopad_metal_target_create(void *window, uint32_t width, uint32_t height, int depth_stencil);
void *halopad_window_host(uint32_t hwnd, uint32_t width, uint32_t height);
void halopad_metal_target_gamma(void *target, const uint16_t ramp[768]);
void halopad_metal_target_destroy(void *t);
void halopad_metal_clear(void *t, int color, int depth, int stencil, const float rgba[4], float z, uint32_t s);
int halopad_metal_present(void *t);
extern _Thread_local _cpu *halopad_cpu;

#define D3D_OK 0u
#define D3DERR_INVALIDCALL 0x8876086Cu
#include "halopad_d3d9_internal.h"
static uint8_t rs_valid[NRS];   /* D3DRENDERSTATETYPE values that exist (set from the default tables) */


void halopad_com_bind(uint32_t g);
void halopad_com_unbind(uint32_t g);
const char *halopad_com_interface(uint32_t g);

static void rebind(uint32_t *slot, uint32_t g)
{
    if (*slot == g) return;
    halopad_com_bind(g);
    halopad_com_unbind(*slot);
    *slot = g;
}

static int is(uint32_t g, const char *iface) { return !g || !strcmp(halopad_com_interface(g), iface); }
static device *dev(uint32_t g) { return halopad_com_state("IDirect3DDevice9", g); }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* Direct3D 9 render-state defaults (d3d9types.h / SDK documentation); 0 = not a state. */
static void default_states(device *d)
{
    static const struct { uint16_t s; uint32_t v; } rs[] = {
        {8, 3}, {9, 2}, {14, 1}, {15, 0}, {16, 1}, {19, 2}, {20, 1}, {22, 3}, {23, 4}, {24, 0}, {25, 8}, {26, 0},
        {27, 0}, {28, 0}, {29, 0}, {34, 0}, {35, 0}, {48, 0}, {52, 0}, {53, 1}, {54, 1}, {55, 1}, {56, 8}, {57, 0},
        {58, 0xFFFFFFFF}, {59, 0xFFFFFFFF}, {60, 0xFFFFFFFF}, {128, 0}, {129, 0}, {130, 0}, {131, 0}, {132, 0}, {133, 0},
        {134, 0}, {135, 0}, {136, 1}, {137, 1}, {139, 0}, {140, 0}, {141, 1}, {142, 1}, {143, 0}, {145, 1}, {146, 2},
        {147, 0}, {148, 0}, {151, 0}, {152, 0}, {156, 0}, {157, 0}, {161, 1}, {162, 0xFFFFFFFF}, {163, 0}, {165, 0},
        {167, 0}, {168, 0xF}, {171, 1}, {172, 3}, {173, 1}, {174, 0}, {175, 0}, {176, 0}, {180, 0}, {181, 0}, {182, 0},
        {184, 0}, {185, 0}, {186, 1}, {187, 1}, {188, 1}, {189, 8}, {190, 0xF}, {191, 0xF}, {192, 0xF},
        {193, 0xFFFFFFFF}, {194, 0}, {195, 0}, {198, 0}, {199, 0}, {200, 0}, {201, 0}, {202, 0}, {203, 0}, {204, 0},
        {205, 0}, {206, 0}, {207, 2}, {208, 1}, {209, 1},
    };
    memset(d->rs, 0, sizeof d->rs);
    for (size_t i = 0; i < sizeof rs / sizeof rs[0]; i++) { d->rs[rs[i].s] = rs[i].v; rs_valid[rs[i].s] = 1; }
    d->rs[7] = d->pp[9] ? 1u : 0u;                                   /* ZENABLE: TRUE with an automatic depth buffer */
    rs_valid[7] = 1;
    static const struct { uint16_t s; float v; } fs[] = {
        {36, 0.0f}, {37, 1.0f}, {38, 1.0f}, {154, 1.0f}, {155, 1.0f}, {158, 1.0f}, {159, 0.0f}, {160, 0.0f},
        {166, 64.0f}, {170, 0.0f}, {178, 1.0f}, {179, 1.0f}, {183, 1.0f},
    };
    for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++) { d->rs[fs[i].s] = fbits(fs[i].v); rs_valid[fs[i].s] = 1; }
    for (int t = 0; t < 8; t++) {
        memset(d->tss[t], 0xFF, sizeof d->tss[t]);
        d->tss[t][1] = t ? 1 : 4;  d->tss[t][2] = 2; d->tss[t][3] = 1;   /* COLOROP, COLORARG1/2 */
        d->tss[t][4] = t ? 1 : 2;  d->tss[t][5] = 2; d->tss[t][6] = 1;   /* ALPHAOP, ALPHAARG1/2 */
        for (int k = 7; k <= 10; k++) d->tss[t][k] = 0;                  /* BUMPENVMAT */
        d->tss[t][11] = (uint32_t)t;                                     /* TEXCOORDINDEX */
        d->tss[t][22] = 0; d->tss[t][23] = 0; d->tss[t][24] = 0;         /* BUMPENVLSCALE/LOFFSET, TEXTURETRANSFORMFLAGS */
        d->tss[t][26] = 1; d->tss[t][27] = 1; d->tss[t][28] = 1;         /* COLORARG0, ALPHAARG0, RESULTARG */
        d->tss[t][32] = 0;                                               /* CONSTANT */
    }
    for (int s = 0; s < NSAMPLERS; s++) {
        uint32_t *v = d->ss[s];
        v[0] = 0xFFFFFFFF;
        v[1] = v[2] = v[3] = 1;                                          /* ADDRESSU/V/W: WRAP */
        v[4] = 0; v[5] = 1; v[6] = 1; v[7] = 0;                          /* BORDERCOLOR, MAG/MIN POINT, MIP NONE */
        v[8] = 0; v[9] = 0; v[10] = 1; v[11] = 0; v[12] = 0; v[13] = 0;  /* LODBIAS, MAXMIPLEVEL, MAXANISOTROPY, SRGB, ELEMENTINDEX, DMAPOFFSET */
    }
    for (int i = 0; i < 512; i++) {
        memset(d->transform[i], 0, sizeof d->transform[i]);
        d->transform[i][0] = d->transform[i][5] = d->transform[i][10] = d->transform[i][15] = 1.0f;
    }
    d->viewport[0] = 0; d->viewport[1] = 0; d->viewport[2] = d->pp[0]; d->viewport[3] = d->pp[1];
    d->viewport[4] = fbits(0.0f); d->viewport[5] = fbits(1.0f);
    for (int c = 0; c < 3; c++) for (int i = 0; i < 256; i++) d->gamma[c][i] = (uint16_t)(i * 257);
    d->cursor_shown = 0;
}

static void destroy(void *p)
{
    device *d = p;
    for (int i = 0; i < 16; i++) { rebind(&d->texture[i], 0); rebind(&d->stream[i], 0); }
    rebind(&d->indices, 0); rebind(&d->decl, 0); rebind(&d->vs, 0); rebind(&d->ps, 0);
    halopad_d3d9_release_targets(d);
    if (d->target) halopad_metal_target_destroy(d->target);
    free(d);
}

/* D3DPRESENT_PARAMETERS as CreateDevice and Reset accept them: validated into out[14], with the
   size and formats Direct3D fills in written back to the caller's copy. 0 or an error. */
static uint32_t present_params(const char *service, uint32_t pp, uint32_t focus, uint32_t out[14], uint32_t *window)
{
    for (int i = 0; i < 14; i++) out[i] = rd32(pp + 4 * i);
    *window = out[7] ? out[7] : focus;
    if (out[4] || out[5]) hp_unsupported(service, "multisampling (type %u, quality %u)", out[4], out[5]);
    if (out[3] > 1) hp_unsupported(service, "%u back buffers", out[3]);
    if (out[6] < 1 || out[6] > 3) return D3DERR_INVALIDCALL;
    if (out[11] & ~0x1u) hp_unsupported(service, "presentation flags 0x%x", out[11]);
    int32_t w = (int32_t)out[0], h = (int32_t)out[1];
    if (out[8] && (!w || !h)) {                                     /* windowed with 0: the window's client area */
        int32_t cw, ch;
        if (!halopad_window_client_size(*window, &cw, &ch)) return D3DERR_INVALIDCALL;
        if (!w) w = cw;
        if (!h) h = ch;
    }
    if (w <= 0 || h <= 0) return D3DERR_INVALIDCALL;
    if (out[2] == 0 && out[8]) out[2] = 22;                          /* windowed UNKNOWN: the display format */
    if (out[2] != 21 && out[2] != 22) hp_unsupported(service, "back buffer format %u", out[2]);
    if (out[9] && out[10] != 75 && out[10] != 77 && out[10] != 80)
        hp_unsupported(service, "depth format %u", out[10]);
    out[0] = (uint32_t)w; out[1] = (uint32_t)h;
    if (!out[3]) out[3] = 1;
    wr32(pp, (uint32_t)w); wr32(pp + 4, (uint32_t)h); wr32(pp + 8, out[2]); wr32(pp + 12, out[3]);   /* as Direct3D fills them in */
    return D3D_OK;
}

uint32_t halopad_d3d9_create_device(uint32_t d3d, uint32_t adapter, uint32_t type, uint32_t focus, uint32_t behavior,
                                    uint32_t pp, uint32_t out)
{
    if (adapter || type != 1) return D3DERR_INVALIDCALL;
    if (behavior & 0x10) return D3DERR_INVALIDCALL;                  /* PUREDEVICE: caps do not offer it */
    uint32_t known = 0x2 | 0x4 | 0x20 | 0x40 | 0x80;                 /* FPU_PRESERVE, MULTITHREADED, SW/HW/MIXED VP */
    if (behavior & ~known) hp_unsupported("IDirect3D9::CreateDevice", "behavior flags 0x%x", behavior);
    int vp = !!(behavior & 0x20) + !!(behavior & 0x40) + !!(behavior & 0x80);
    if (vp != 1) return D3DERR_INVALIDCALL;
    device *d = calloc(1, sizeof *d);
    uint32_t window, r = present_params("IDirect3D9::CreateDevice", pp, focus, d->pp, &window);
    if (r) { free(d); return r; }
    d->d3d = d3d; d->window = window; d->behavior = behavior; d->software_vp = !!(behavior & 0x20);
    default_states(d);
    d->target = halopad_metal_target_create(halopad_window_host(window, d->pp[0], d->pp[1]), d->pp[0], d->pp[1], d->pp[9] != 0);
    /* Without D3DCREATE_FPU_PRESERVE, Direct3D 9 sets the x87 unit to single precision and
       round-to-nearest for the calling thread. */
    if (!(behavior & 0x2) && halopad_cpu) halopad_cpu->_st_cw &= ~0x0F00u;
    d->guest = halopad_com_new("IDirect3DDevice9", 4, d, destroy);
    halopad_d3d9_create_targets(d);
    halopad_com_addref(d3d);
    wr32(out, d->guest);
    return D3D_OK;
}

/* Reset: Direct3D 9 refuses while the application still holds default-pool resources, explicit
   render targets or depth surfaces, state blocks, or references to the implicit back buffer or
   depth buffer. Otherwise every device state returns to its default (bindings released, lights,
   material, shader constants and clip state cleared, the viewport the whole new back buffer),
   the back buffer and depth buffer are recreated from the new parameters, and the device is out
   of any scene. */
uint32_t halopad_d3d9_live_default(uint32_t dev);   /* halopad_d3d9_resources.c */
uint32_t halopad_d3d9_live_stateblocks(uint32_t dev);   /* halopad_d3d9_stateblock.c */
static uint32_t refs_of(uint32_t g) { if (!g) return 0; uint32_t n = halopad_com_addref(g); halopad_com_release(g); return n - 1; }

uint32_t hpcom_IDirect3DDevice9_Reset_c(uint32_t g, uint32_t pp)
{
    device *d = dev(g);
    if (!pp) return D3DERR_INVALIDCALL;
    if (halopad_d3d9_live_default(g) || halopad_d3d9_live_stateblocks(g) || refs_of(d->backbuffer) > 1 || refs_of(d->autods) > 1) {
        HP_TRACE_FAIL("IDirect3DDevice9::Reset with %u default-pool resources and %u state blocks alive", halopad_d3d9_live_default(g),
                      halopad_d3d9_live_stateblocks(g));
        return D3DERR_INVALIDCALL;
    }
    uint32_t npp[14], window;
    uint32_t r = present_params("IDirect3DDevice9::Reset", pp, d->window, npp, &window);
    if (r) return r;
    for (int i = 0; i < 16; i++) { rebind(&d->texture[i], 0); rebind(&d->stream[i], 0); }
    rebind(&d->indices, 0); rebind(&d->decl, 0); rebind(&d->vs, 0); rebind(&d->ps, 0);
    halopad_d3d9_release_targets(d);
    if (d->target) halopad_metal_target_destroy(d->target);
    uint32_t guest = d->guest, d3d = d->d3d, behavior = d->behavior;
    int software_vp = d->software_vp;
    memset(d, 0, sizeof *d);                                        /* every state back to its default */
    d->guest = guest; d->d3d = d3d; d->behavior = behavior; d->software_vp = software_vp; d->window = window;
    memcpy(d->pp, npp, sizeof npp);
    default_states(d);
    d->target = halopad_metal_target_create(halopad_window_host(window, d->pp[0], d->pp[1]), d->pp[0], d->pp[1], d->pp[9] != 0);
    halopad_d3d9_create_targets(d);
    return D3D_OK;
}

uint32_t hpcom_IDirect3DDevice9_AddRef_c(uint32_t g) { dev(g); return halopad_com_addref(g); }
uint32_t hpcom_IDirect3DDevice9_Release_c(uint32_t g)
{
    device *d = dev(g);
    uint32_t d3d = d->d3d, n = halopad_com_release(g);
    if (!n) halopad_com_release(d3d);
    return n;
}
uint32_t hpcom_IDirect3DDevice9_TestCooperativeLevel_c(uint32_t g) { dev(g); return D3D_OK; }
uint32_t hpcom_IDirect3DDevice9_GetAvailableTextureMem_c(uint32_t g) { dev(g); return 128u << 20; }   /* the card class: 128 MB */
uint32_t hpcom_IDirect3DDevice9_GetDirect3D_c(uint32_t g, uint32_t out)
{
    device *d = dev(g);
    halopad_com_addref(d->d3d);
    wr32(out, d->d3d);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetDeviceCaps_c(uint32_t g, uint32_t caps) { dev(g); halopad_d3d9_fill_caps(caps); return D3D_OK; }
uint32_t hpcom_IDirect3DDevice9_GetDisplayMode_c(uint32_t g, uint32_t swap, uint32_t mode)
{
    device *d = dev(g);
    if (swap) return D3DERR_INVALIDCALL;
    if (d->pp[8]) {
        int32_t w, h;
        halopad_desktop_size(&w, &h);
        wr32(mode, (uint32_t)w); wr32(mode + 4, (uint32_t)h); wr32(mode + 8, 60); wr32(mode + 12, 22);
    } else {
        wr32(mode, d->pp[0]); wr32(mode + 4, d->pp[1]); wr32(mode + 8, d->pp[12] ? d->pp[12] : 60); wr32(mode + 12, d->pp[2]);
    }
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetCreationParameters_c(uint32_t g, uint32_t out)
{
    device *d = dev(g);
    wr32(out, 0); wr32(out + 4, 1); wr32(out + 8, d->window); wr32(out + 12, d->behavior);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_ShowCursor_c(uint32_t g, uint32_t show)
{
    device *d = dev(g);
    int old = d->cursor_shown;
    d->cursor_shown = show != 0;
    return (uint32_t)old;
}
uint32_t hpcom_IDirect3DDevice9_SetGammaRamp_c(uint32_t g, uint32_t swap, uint32_t flags, uint32_t ramp)
{
    device *d = dev(g);
    (void)flags;                                                    /* D3DSGR_CALIBRATE: no calibration to apply */
    if (swap) return D3DERR_INVALIDCALL;
    memcpy(d->gamma, G(ramp), sizeof d->gamma);
    halopad_metal_target_gamma(d->target, &d->gamma[0][0]);        /* applied when the back buffer is shown */
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetGammaRamp_c(uint32_t g, uint32_t swap, uint32_t ramp)
{
    device *d = dev(g);
    if (swap) return D3DERR_INVALIDCALL;
    memcpy(G(ramp), d->gamma, sizeof d->gamma);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_BeginScene_c(uint32_t g)
{
    device *d = dev(g);
    if (d->in_scene) return D3DERR_INVALIDCALL;
    d->in_scene = 1;
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_EndScene_c(uint32_t g)
{
    device *d = dev(g);
    if (!d->in_scene) return D3DERR_INVALIDCALL;
    d->in_scene = 0;
    return D3D_OK;
}
int halopad_d3d9_tracing(void);
extern uint32_t halopad_d3d9_frame;
uint32_t hpcom_IDirect3DDevice9_Clear_c(uint32_t g, uint32_t count, uint32_t rects, uint32_t flags, uint32_t color, uint32_t z,
                                        uint32_t stencil)
{
    device *d = dev(g);
    if (halopad_d3d9_tracing()) fprintf(stderr, "HALOPAD DRAW f%u clear flags %x color %08x z %08x stencil %u rt %08x\n", halopad_d3d9_frame, flags, color, z, stencil, d->rt);
    if (count || rects) hp_unsupported("IDirect3DDevice9::Clear", "%u rectangles", count);
    if (flags & ~0x7u) return D3DERR_INVALIDCALL;
    hp_bound b = halopad_d3d9_bind_targets(d);
    if ((flags & 0x6) && !b.depth) return D3DERR_INVALIDCALL;        /* no depth/stencil surface */
    uint32_t v[6];
    memcpy(v, d->viewport, sizeof v);
    if (v[0] || v[1] || v[2] != b.width || v[3] != b.height)
        hp_unsupported("IDirect3DDevice9::Clear", "a clear limited to viewport %ux%u at %u,%u", v[2], v[3], v[0], v[1]);
    if ((flags & 0x6) && (b.width != d->pp[0] || b.height != d->pp[1]))
        hp_unsupported("IDirect3DDevice9::Clear", "depth/stencil clear with a %ux%u render target on the larger depth buffer", b.width, b.height);
    float rgba[4] = {((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f, (color >> 24) / 255.0f};
    if (b.format == 22) rgba[3] = 1.0f;                              /* X8R8G8B8 keeps alpha 1 */
    float zf;
    memcpy(&zf, &z, 4);
    halopad_metal_clear(d->target, flags & 1, (flags >> 1) & 1, (flags >> 2) & 1, rgba, zf, stencil);
    return D3D_OK;
}
/* Test support: called with each device just before it presents (the frame is complete). */
void (*halopad_d3d9_present_hook)(uint32_t device);

/* Diagnostics: HALOPAD_TRACE_DRAWS=first:last prints every draw, clear and render-target change
   in those frames (counted by Present, from 1), with the guest return addresses on the stack. */
uint32_t halopad_d3d9_frame = 1;
int halopad_d3d9_tracing(void)
{
    static int init, lo = -1, hi = -1;
    if (!init) {
        init = 1;
        const char *e = getenv("HALOPAD_TRACE_DRAWS");
        if (e && sscanf(e, "%d:%d", &lo, &hi) != 2) lo = hi = -1;
    }
    return lo >= 0 && (int)halopad_d3d9_frame >= lo && (int)halopad_d3d9_frame <= hi;
}
void halopad_d3d9_trace_callers(char *out, size_t n)
{
    size_t k = 0;
    out[0] = 0;
    if (!halopad_cpu) return;
    uint32_t sp = halopad_cpu->_esp;
    for (int i = 0, found = 0; i < 64 && found < 3; i++) {
        uint32_t v = rd32(sp + 4 * (uint32_t)i);
        if (v >= 0x401000 && v < 0x5df000 && k + 12 < n) { k += (size_t)snprintf(out + k, n - k, " %06x", v); found++; }
    }
}

uint32_t hpcom_IDirect3DDevice9_Present_c(uint32_t g, uint32_t src, uint32_t dst, uint32_t window, uint32_t dirty)
{
    device *d = dev(g);
    if (src || dst || (window && window != d->window) || dirty)
        hp_unsupported("IDirect3DDevice9::Present", "source/destination rectangles, another window or a dirty region");
    if (halopad_d3d9_present_hook) halopad_d3d9_present_hook(g);
    halopad_metal_present(d->target);
    halopad_d3d9_frame++;
    return D3D_OK;
}

uint32_t hpcom_IDirect3DDevice9_SetRenderState_c(uint32_t g, uint32_t s, uint32_t v)
{
    device *d = dev(g);
    if (s >= NRS || !rs_valid[s]) hp_unsupported("IDirect3DDevice9::SetRenderState", "state %u (value 0x%x)", s, v);
    W(d)->rs[s] = v;
    MARK(d, rs[s]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetRenderState_c(uint32_t g, uint32_t s, uint32_t out)
{
    device *d = dev(g);
    if (s >= NRS || !rs_valid[s]) return D3DERR_INVALIDCALL;
    wr32(out, d->rs[s]);
    return D3D_OK;
}
static int valid_tss(uint32_t s) { return (s >= 1 && s <= 11) || s == 22 || s == 23 || s == 24 || s == 26 || s == 27 || s == 28 || s == 32; }
int halopad_d3d9_tss_valid(uint32_t s) { return valid_tss(s); }
int halopad_d3d9_rs_valid(uint32_t s) { return s < NRS && rs_valid[s]; }
uint32_t hpcom_IDirect3DDevice9_SetTextureStageState_c(uint32_t g, uint32_t stage, uint32_t s, uint32_t v)
{
    device *d = dev(g);
    if (stage >= 8 || !valid_tss(s)) hp_unsupported("IDirect3DDevice9::SetTextureStageState", "stage %u state %u (value 0x%x)", stage, s, v);
    W(d)->tss[stage][s] = v;
    MARK(d, tss[stage][s]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetTextureStageState_c(uint32_t g, uint32_t stage, uint32_t s, uint32_t out)
{
    device *d = dev(g);
    if (stage >= 8 || !valid_tss(s)) return D3DERR_INVALIDCALL;
    wr32(out, d->tss[stage][s]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_SetSamplerState_c(uint32_t g, uint32_t sampler, uint32_t s, uint32_t v)
{
    device *d = dev(g);
    if (sampler >= NSAMPLERS || s < 1 || s >= NSS)
        hp_unsupported("IDirect3DDevice9::SetSamplerState", "sampler %u state %u (value 0x%x)", sampler, s, v);
    W(d)->ss[sampler][s] = v;
    MARK(d, ss[sampler][s]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetSamplerState_c(uint32_t g, uint32_t sampler, uint32_t s, uint32_t out)
{
    device *d = dev(g);
    if (sampler >= NSAMPLERS || s < 1 || s >= NSS) return D3DERR_INVALIDCALL;
    wr32(out, d->ss[sampler][s]);
    return D3D_OK;
}
static int transform_index(uint32_t s) { return (s == 2 || s == 3 || (s >= 16 && s <= 23) || (s >= 256 && s < 512)) ? (int)s : -1; }
uint32_t hpcom_IDirect3DDevice9_SetTransform_c(uint32_t g, uint32_t s, uint32_t m)
{
    device *d = dev(g);
    int i = transform_index(s);
    if (i < 0) hp_unsupported("IDirect3DDevice9::SetTransform", "transform state %u", s);
    memcpy(W(d)->transform[i], G(m), 64);
    MARK(d, transform[i]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetTransform_c(uint32_t g, uint32_t s, uint32_t m)
{
    device *d = dev(g);
    int i = transform_index(s);
    if (i < 0) return D3DERR_INVALIDCALL;
    memcpy(G(m), d->transform[i], 64);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_SetViewport_c(uint32_t g, uint32_t vp)
{
    device *d = dev(g);
    uint32_t v[6];
    memcpy(v, G(vp), sizeof v);
    hp_bound b = halopad_d3d9_bind_targets(d);
    if (v[0] + v[2] > b.width || v[1] + v[3] > b.height || !v[2] || !v[3]) return D3DERR_INVALIDCALL;
    memcpy(W(d)->viewport, v, sizeof v);
    MARK(d, viewport);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetViewport_c(uint32_t g, uint32_t vp) { device *d = dev(g); memcpy(G(vp), d->viewport, 24); return D3D_OK; }
uint32_t hpcom_IDirect3DDevice9_SetSoftwareVertexProcessing_c(uint32_t g, uint32_t sw)
{
    device *d = dev(g);
    if (!(d->behavior & 0x80)) return D3DERR_INVALIDCALL;            /* only mixed-mode devices switch */
    d->software_vp = sw != 0;
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetSoftwareVertexProcessing_c(uint32_t g) { return (uint32_t)dev(g)->software_vp; }

/* ---- bindings ---- */

uint32_t hpcom_IDirect3DDevice9_SetTexture_c(uint32_t g, uint32_t stage, uint32_t tex)
{
    device *d = dev(g);
    if (stage >= 16) hp_unsupported("IDirect3DDevice9::SetTexture", "sampler %u (displacement or vertex texture)", stage);
    if (!is(tex, "IDirect3DTexture9") && !is(tex, "IDirect3DCubeTexture9") && !is(tex, "IDirect3DVolumeTexture9"))
        return D3DERR_INVALIDCALL;
    rebind(&W(d)->texture[stage], tex);
    MARK(d, texture[stage]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetTexture_c(uint32_t g, uint32_t stage, uint32_t out)
{
    device *d = dev(g);
    if (stage >= 16) return D3DERR_INVALIDCALL;
    if (d->texture[stage]) halopad_com_addref(d->texture[stage]);
    wr32(out, d->texture[stage]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_SetStreamSource_c(uint32_t g, uint32_t s, uint32_t vb, uint32_t offset, uint32_t stride)
{
    device *d = dev(g);
    if (s >= 16 || !is(vb, "IDirect3DVertexBuffer9") || (offset & 3)) return D3DERR_INVALIDCALL;
    rebind(&W(d)->stream[s], vb);
    W(d)->stream_offset[s] = offset;
    W(d)->stream_stride[s] = stride;
    MARK(d, stream[s]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetStreamSource_c(uint32_t g, uint32_t s, uint32_t out, uint32_t offset, uint32_t stride)
{
    device *d = dev(g);
    if (s >= 16) return D3DERR_INVALIDCALL;
    if (d->stream[s]) halopad_com_addref(d->stream[s]);
    wr32(out, d->stream[s]); wr32(offset, d->stream_offset[s]); wr32(stride, d->stream_stride[s]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_SetStreamSourceFreq_c(uint32_t g, uint32_t s, uint32_t div)
{
    dev(g);
    if (s >= 16) return D3DERR_INVALIDCALL;
    if (div != 1) hp_unsupported("IDirect3DDevice9::SetStreamSourceFreq", "stream %u divider 0x%x (instancing)", s, div);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_SetIndices_c(uint32_t g, uint32_t ib)
{
    device *d = dev(g);
    if (!is(ib, "IDirect3DIndexBuffer9")) return D3DERR_INVALIDCALL;
    rebind(&W(d)->indices, ib);
    MARK(d, indices);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetIndices_c(uint32_t g, uint32_t out)
{
    device *d = dev(g);
    if (d->indices) halopad_com_addref(d->indices);
    wr32(out, d->indices);
    return D3D_OK;
}
/* SetFVF and SetVertexDeclaration each replace the other's input layout. */
uint32_t hpcom_IDirect3DDevice9_SetVertexDeclaration_c(uint32_t g, uint32_t decl)
{
    device *d = dev(g);
    if (!is(decl, "IDirect3DVertexDeclaration9")) return D3DERR_INVALIDCALL;
    rebind(&W(d)->decl, decl);
    W(d)->fvf = 0;
    MARK(d, layout);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetVertexDeclaration_c(uint32_t g, uint32_t out)
{
    device *d = dev(g);
    if (d->decl) halopad_com_addref(d->decl);
    wr32(out, d->decl);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_SetFVF_c(uint32_t g, uint32_t fvf)
{
    device *d = dev(g);
    W(d)->fvf = fvf;
    if (fvf) rebind(&W(d)->decl, 0);
    MARK(d, layout);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetFVF_c(uint32_t g, uint32_t out) { wr32(out, dev(g)->fvf); return D3D_OK; }
uint32_t hpcom_IDirect3DDevice9_SetVertexShader_c(uint32_t g, uint32_t vs)
{
    device *d = dev(g);
    if (!is(vs, "IDirect3DVertexShader9")) return D3DERR_INVALIDCALL;
    rebind(&W(d)->vs, vs);
    MARK(d, vs);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetVertexShader_c(uint32_t g, uint32_t out)
{
    device *d = dev(g);
    if (d->vs) halopad_com_addref(d->vs);
    wr32(out, d->vs);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_SetPixelShader_c(uint32_t g, uint32_t ps)
{
    device *d = dev(g);
    if (!is(ps, "IDirect3DPixelShader9")) return D3DERR_INVALIDCALL;
    rebind(&W(d)->ps, ps);
    MARK(d, ps);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetPixelShader_c(uint32_t g, uint32_t out)
{
    device *d = dev(g);
    if (d->ps) halopad_com_addref(d->ps);
    wr32(out, d->ps);
    return D3D_OK;
}

/* Shader constants: set/get 'count' 4-component registers (Booleans: single values). */
static uint32_t constants(void *store, uint32_t limit, uint32_t elem, uint32_t start, uint32_t data, uint32_t count, int set)
{
    if (start > limit || count > limit - start) return D3DERR_INVALIDCALL;
    uint8_t *base = (uint8_t *)store + (size_t)start * elem;
    if (set) memcpy(base, G(data), (size_t)count * elem);
    else memcpy(G(data), base, (size_t)count * elem);
    return D3D_OK;
}
/* setters: into the device, or into the recording state block (marking the registers) */
#define SETC(field, limit, elem) do { device *d = dev(g); uint32_t r = constants(W(d)->field, limit, elem, s, p, n, 1); \
        if (r == D3D_OK && d->rec) memset(d->rec->m.field + s, 1, n); return r; } while (0)
uint32_t hpcom_IDirect3DDevice9_SetVertexShaderConstantF_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { SETC(vsf, 256, 16); }
uint32_t hpcom_IDirect3DDevice9_GetVertexShaderConstantF_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { return constants(dev(g)->vsf, 256, 16, s, p, n, 0); }
uint32_t hpcom_IDirect3DDevice9_SetVertexShaderConstantI_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { SETC(vsi, 16, 16); }
uint32_t hpcom_IDirect3DDevice9_GetVertexShaderConstantI_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { return constants(dev(g)->vsi, 16, 16, s, p, n, 0); }
uint32_t hpcom_IDirect3DDevice9_SetVertexShaderConstantB_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { SETC(vsb, 16, 4); }
uint32_t hpcom_IDirect3DDevice9_GetVertexShaderConstantB_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { return constants(dev(g)->vsb, 16, 4, s, p, n, 0); }
uint32_t hpcom_IDirect3DDevice9_SetPixelShaderConstantF_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { SETC(psf, 224, 16); }
uint32_t hpcom_IDirect3DDevice9_GetPixelShaderConstantF_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { return constants(dev(g)->psf, 224, 16, s, p, n, 0); }
uint32_t hpcom_IDirect3DDevice9_SetPixelShaderConstantI_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { SETC(psi, 16, 16); }
uint32_t hpcom_IDirect3DDevice9_GetPixelShaderConstantI_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { return constants(dev(g)->psi, 16, 16, s, p, n, 0); }
uint32_t hpcom_IDirect3DDevice9_SetPixelShaderConstantB_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { SETC(psb, 16, 4); }
uint32_t hpcom_IDirect3DDevice9_GetPixelShaderConstantB_c(uint32_t g, uint32_t s, uint32_t p, uint32_t n) { return constants(dev(g)->psb, 16, 4, s, p, n, 0); }

/* ---- fixed-function material and lights (used by halopad_d3d9_ff.c) ---- */

uint32_t hpcom_IDirect3DDevice9_SetMaterial_c(uint32_t g, uint32_t m) { device *d = dev(g); memcpy(W(d)->material, G(m), 68); MARK(d, material); return D3D_OK; }
uint32_t hpcom_IDirect3DDevice9_GetMaterial_c(uint32_t g, uint32_t m) { memcpy(G(m), dev(g)->material, 68); return D3D_OK; }

int halopad_d3d9_light_slot(device *d, uint32_t index, int create);
static int light_slot(device *d, uint32_t index, int create) { return halopad_d3d9_light_slot(d, index, create); }
int halopad_d3d9_light_slot(device *d, uint32_t index, int create)
{
    for (uint32_t i = 0; i < d->nlight; i++) if (d->light[i].index == index) return (int)i;
    if (!create) return -1;
    if (d->nlight == 16) hp_unsupported("IDirect3DDevice9::SetLight", "more than 16 lights");
    int i = (int)d->nlight++;
    memset(&d->light[i], 0, sizeof d->light[i]);
    d->light[i].index = index;
    /* the light LightEnable creates for an unset index: white directional light along +z */
    uint32_t dir = 3;
    memcpy(&d->light[i].light[0], &dir, 4);
    d->light[i].light[1] = d->light[i].light[2] = d->light[i].light[3] = 1.0f;   /* diffuse rgb */
    d->light[i].light[18] = 1.0f;                                                 /* direction z */
    return i;
}
uint32_t hpcom_IDirect3DDevice9_SetLight_c(uint32_t g, uint32_t index, uint32_t l)
{
    device *d = dev(g);
    uint32_t type = rd32(l);
    if (type < 1 || type > 3) return D3DERR_INVALIDCALL;
    device *t = W(d);
    int i = light_slot(t, index, 1);
    memcpy(t->light[i].light, G(l), 104);
    t->light[i].set = 1;
    MARK(d, light[i]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetLight_c(uint32_t g, uint32_t index, uint32_t l)
{
    device *d = dev(g);
    int i = light_slot(d, index, 0);
    if (i < 0 || !d->light[i].set) return D3DERR_INVALIDCALL;
    memcpy(G(l), d->light[i].light, 104);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_LightEnable_c(uint32_t g, uint32_t index, uint32_t on)
{
    device *d = dev(g);
    device *t = W(d);
    int i = light_slot(t, index, 1);
    t->light[i].enabled = on != 0;
    MARK(d, light_enable[i]);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetLightEnable_c(uint32_t g, uint32_t index, uint32_t out)
{
    device *d = dev(g);
    int i = light_slot(d, index, 0);
    if (i < 0) return D3DERR_INVALIDCALL;
    wr32(out, d->light[i].enabled ? 128u : 0u);                     /* Direct3D 9 reports enabled as 128 */
    return D3D_OK;
}

/* Test support: the device's Metal target. */
void *halopad_d3d9_device_target(uint32_t g) { return dev(g)->target; }
