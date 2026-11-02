/* Direct3D 9 device methods Tribes: Vengeance's D3DDrv uses that Halo did not
 * (port/vengeance/d3d9 is HaloPad's device): scissor rectangle, user clip
 * planes, render-target and depth surfaces of their own, matrix products, and
 * queries whose answers are fixed for this device. */
#include "d3d9/halopad_win32.h"
#include "d3d9/halopad_d3d9_internal.h"

void *halopad_com_state(const char *iface, uint32_t g);
void *halopad_metal_render_texture(uint32_t format, uint32_t w, uint32_t h, uint32_t levels);
void *halopad_metal_depth_texture(uint32_t w, uint32_t h);
uint32_t hpcom_IDirect3DDevice9_GetTransform_c(uint32_t g, uint32_t s, uint32_t m);
uint32_t hpcom_IDirect3DDevice9_SetTransform_c(uint32_t g, uint32_t s, uint32_t m);
#define D3D_OK 0u
#define D3DERR_INVALIDCALL 0x8876086Cu

static device *dev(uint32_t g) { return halopad_com_state("IDirect3DDevice9", g); }

uint32_t hpcom_IDirect3DDevice9_SetScissorRect_c(uint32_t g, uint32_t rect)
{
    device *d = dev(g);
    if (!rect) return D3DERR_INVALIDCALL;
    memcpy(d->scissor, G(rect), 16);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetScissorRect_c(uint32_t g, uint32_t rect)
{
    device *d = dev(g);
    if (!rect) return D3DERR_INVALIDCALL;
    memcpy(G(rect), d->scissor, 16);
    return D3D_OK;
}
/* User clip planes are kept; drawing with any enabled still stops (halopad_d3d9_draw.c). */
uint32_t hpcom_IDirect3DDevice9_SetClipPlane_c(uint32_t g, uint32_t index, uint32_t plane)
{
    device *d = dev(g);
    if (index >= 6 || !plane) return D3DERR_INVALIDCALL;
    memcpy(d->clip[index], G(plane), 16);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_GetClipPlane_c(uint32_t g, uint32_t index, uint32_t plane)
{
    device *d = dev(g);
    if (index >= 6 || !plane) return D3DERR_INVALIDCALL;
    memcpy(G(plane), d->clip[index], 16);
    return D3D_OK;
}
/* MultiplyTransform(state, m): the state's matrix becomes m times it. */
uint32_t hpcom_IDirect3DDevice9_MultiplyTransform_c(uint32_t g, uint32_t s, uint32_t m)
{
    static uint32_t scratch;
    if (!scratch) scratch = halopad_heap_alloc(64, 1);
    uint32_t r = hpcom_IDirect3DDevice9_GetTransform_c(g, s, scratch);
    if (r) return r;
    float a[16], b[16], c[16];
    memcpy(a, G(m), 64);
    memcpy(b, G(scratch), 64);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) c[4 * i + j] = a[4 * i] * b[j] + a[4 * i + 1] * b[4 + j] + a[4 * i + 2] * b[8 + j] + a[4 * i + 3] * b[12 + j];
    memcpy(G(scratch), c, 64);
    return hpcom_IDirect3DDevice9_SetTransform_c(g, s, scratch);
}
uint32_t hpcom_IDirect3DDevice9_CreateRenderTarget_c(uint32_t g, uint32_t w, uint32_t h, uint32_t format, uint32_t ms, uint32_t quality,
                                                     uint32_t lockable, uint32_t out, uint32_t shared)
{
    dev(g);
    (void)quality;
    if (!w || !h || !out || shared || ms > 1 || lockable || (format != 21 && format != 22 && format != 23)) return D3DERR_INVALIDCALL;
    uint32_t s = halopad_d3d9_surface_new(g, w, h, format, 0x1, 0, NULL, 0);
    ((res *)halopad_com_state("IDirect3DSurface9", s))->native = halopad_metal_render_texture(format == 23 ? 40u : 80u, w, h, 1);
    wr32(out, s);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_CreateDepthStencilSurface_c(uint32_t g, uint32_t w, uint32_t h, uint32_t format, uint32_t ms, uint32_t quality,
                                                            uint32_t discard, uint32_t out, uint32_t shared)
{
    dev(g);
    (void)quality; (void)discard;
    if (!w || !h || !out || shared || ms > 1 || (format != 75 && format != 77 && format != 80)) return D3DERR_INVALIDCALL;
    uint32_t s = halopad_d3d9_surface_new(g, w, h, format, 0x2, 0, NULL, 0);
    ((res *)halopad_com_state("IDirect3DSurface9", s))->native = halopad_metal_depth_texture(w, h);
    wr32(out, s);
    return D3D_OK;
}
/* N-patches are not offered (caps): only "off" is accepted. */
uint32_t hpcom_IDirect3DDevice9_SetNPatchMode_c(uint32_t g, uint32_t segments)
{
    dev(g);
    return segments ? D3DERR_INVALIDCALL : D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_ValidateDevice_c(uint32_t g, uint32_t passes)
{
    dev(g);
    if (passes) wr32(passes, 1);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DDevice9_EvictManagedResources_c(uint32_t g) { dev(g); return D3D_OK; }
uint32_t hpcom_IDirect3DDevice9_GetNumberOfSwapChains_c(uint32_t g) { dev(g); return 1; }
uint32_t hpcom_IDirect3DDevice9_SetDialogBoxMode_c(uint32_t g, uint32_t enable) { dev(g); (void)enable; return D3D_OK; }
uint32_t hpcom_IDirect3DDevice9_GetRasterStatus_c(uint32_t g, uint32_t swap, uint32_t status)
{
    dev(g);
    if (swap || !status) return D3DERR_INVALIDCALL;
    wr32(status, 0); wr32(status + 4, 0);                                /* not in vertical blank, scan line 0 */
    return D3D_OK;
}
/* The hardware cursor: the game draws its own pointer, so these take no effect. */
uint32_t hpcom_IDirect3DDevice9_SetCursorProperties_c(uint32_t g, uint32_t x, uint32_t y, uint32_t surface) { dev(g); (void)x; (void)y; (void)surface; return D3D_OK; }
uint32_t hpcom_IDirect3DDevice9_SetCursorPosition_c(uint32_t g, uint32_t x, uint32_t y, uint32_t flags) { dev(g); (void)x; (void)y; (void)flags; return 0; }

