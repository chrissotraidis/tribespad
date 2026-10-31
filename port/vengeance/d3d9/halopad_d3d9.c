/* HaloPad Direct3D 9, part 1 (G3/G4): Direct3DCreate9 and IDirect3D9.
 * Answers follow docs/GRAPHICS-CONTRACT.md: the adapter identity selects Halo's own
 * config.txt tuning (ATI Radeon 9700 PRO); capabilities, formats and modes are promises
 * about HaloPad's Metal layer and are reported absent otherwise. The device itself is in
 * halopad_d3d9_device.c. */
#include "halopad_win32.h"

uint32_t halopad_com_new(const char *iface, uint32_t size, void *state, void (*destroy)(void *));
uint32_t halopad_com_addref(uint32_t g);
uint32_t halopad_com_release(uint32_t g);
void *halopad_com_state(const char *iface, uint32_t g);
void halopad_desktop_size(int32_t *w, int32_t *h);

#define D3D_OK 0u
#define D3DERR_NOTAVAILABLE 0x8876086Au
#define D3DERR_INVALIDCALL 0x8876086Cu
#define D3DOK_NOAUTOGEN 0x0876019Fu
#define HAL 1u

enum {
    F_UNKNOWN = 0, F_A8R8G8B8 = 21, F_X8R8G8B8 = 22, F_R5G6B5 = 23, F_X1R5G5B5 = 24, F_A1R5G5B5 = 25, F_A4R4G4B4 = 26,
    F_A8 = 28, F_L8 = 50, F_A8L8 = 51, F_V8U8 = 60, F_Q8W8V8U8 = 63, F_D24S8 = 75, F_D24X8 = 77, F_D16 = 80,
};
#define F_DXT(n) (0x30545844u + ((uint32_t)(n) << 24))   /* 'DXTn' */

static int is_display(uint32_t f) { return f == F_X8R8G8B8 || f == F_R5G6B5; }
static int is_rt(uint32_t f) { return f == F_X8R8G8B8 || f == F_A8R8G8B8 || f == F_R5G6B5; }
static int is_depth(uint32_t f) { return f == F_D24S8 || f == F_D24X8 || f == F_D16; }
static int is_dxt(uint32_t f) { return f >= F_DXT(1) && f <= F_DXT(5) && (f & 0x00FFFFFFu) == 0x00545844u; }
static int is_texture(uint32_t f)
{
    switch (f) {
    case F_A8R8G8B8: case F_X8R8G8B8: case F_R5G6B5: case F_X1R5G5B5: case F_A1R5G5B5: case F_A4R4G4B4:
    case F_A8: case F_L8: case F_A8L8: case F_V8U8: case F_Q8W8V8U8: return 1;
    }
    return is_dxt(f);
}

uint32_t Direct3DCreate9_c(uint32_t sdk)
{
    if (sdk != 31 && sdk != 32) hp_unsupported("Direct3DCreate9", "SDK version %u", sdk);
    return halopad_com_new("IDirect3D9", 4, NULL, NULL);
}

static void self(uint32_t g) { halopad_com_state("IDirect3D9", g); }

uint32_t hpcom_IDirect3D9_QueryInterface_c(uint32_t g, uint32_t iid, uint32_t out)
{
    (void)out; self(g);
    hp_unsupported("IDirect3D9::QueryInterface", "interface %08x-...", rd32(iid));
}
uint32_t hpcom_IDirect3D9_AddRef_c(uint32_t g) { self(g); return halopad_com_addref(g); }
uint32_t hpcom_IDirect3D9_Release_c(uint32_t g) { self(g); return halopad_com_release(g); }
uint32_t hpcom_IDirect3D9_RegisterSoftwareDevice_c(uint32_t g, uint32_t fn)
{
    self(g); hp_unsupported("IDirect3D9::RegisterSoftwareDevice", "software device 0x%08x", fn);
}
uint32_t hpcom_IDirect3D9_GetAdapterCount_c(uint32_t g) { self(g); return 1; }

uint32_t hpcom_IDirect3D9_GetAdapterIdentifier_c(uint32_t g, uint32_t adapter, uint32_t flags, uint32_t id)
{
    self(g);
    if (adapter) return D3DERR_INVALIDCALL;
    memset(G(id), 0, 0x44C);
    strcpy((char *)G(id), "ati2dvag.dll");
    strcpy((char *)G(id) + 0x200, "RADEON 9700 PRO");
    strcpy((char *)G(id) + 0x400, "\\\\.\\DISPLAY1");
    wr32(id + 0x420, (10u << 16) | 6467u);          /* DriverVersion 6.14.10.6467 */
    wr32(id + 0x424, (6u << 16) | 14u);
    wr32(id + 0x428, 0x1002);                        /* VendorId: ATI */
    wr32(id + 0x42C, 0x4E44);                        /* DeviceId: Radeon 9700 PRO */
    wr32(id + 0x430, 0);
    wr32(id + 0x434, 0);
    static const uint8_t guid[16] = {0x48, 0x61, 0x6C, 0x6F, 0x50, 0x61, 0x64, 0x20, 0x44, 0x33, 0x44, 0x39, 0x00, 0x00, 0x00, 0x01};
    memcpy((uint8_t *)G(id) + 0x438, guid, 16);      /* fixed identifier ("HaloPad D3D9") */
    wr32(id + 0x448, (flags & 2u) ? 1u : 0u);         /* WHQLLevel when D3DENUM_WHQL_LEVEL is asked */
    return D3D_OK;
}

/* Display modes: standard sizes up to the Mac's main display, plus its own size; 60 Hz. */
static uint32_t modes(uint32_t (*out)[2], uint32_t max)
{
    static const uint32_t std[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 720}, {1280, 800}, {1280, 960},
                                      {1280, 1024}, {1440, 900}, {1600, 900}, {1600, 1200}, {1680, 1050}, {1920, 1080},
                                      {1920, 1200}, {2560, 1440}, {2560, 1600}};
    int32_t dw, dh;
    halopad_desktop_size(&dw, &dh);
    uint32_t n = 0;
    for (uint32_t i = 0; i < sizeof std / sizeof std[0] && n < max; i++)
        if (std[i][0] <= (uint32_t)dw && std[i][1] <= (uint32_t)dh) { out[n][0] = std[i][0]; out[n][1] = std[i][1]; n++; }
    int have = 0;
    for (uint32_t i = 0; i < n; i++) have |= out[i][0] == (uint32_t)dw && out[i][1] == (uint32_t)dh;
    if (!have && n < max) {                           /* insert the display's own size in order */
        uint32_t k = n;
        while (k > 0 && (out[k - 1][0] > (uint32_t)dw || (out[k - 1][0] == (uint32_t)dw && out[k - 1][1] > (uint32_t)dh))) {
            out[k][0] = out[k - 1][0]; out[k][1] = out[k - 1][1]; k--;
        }
        out[k][0] = (uint32_t)dw; out[k][1] = (uint32_t)dh; n++;
    }
    return n;
}

uint32_t hpcom_IDirect3D9_GetAdapterModeCount_c(uint32_t g, uint32_t adapter, uint32_t format)
{
    self(g);
    if (adapter || !is_display(format)) return 0;
    uint32_t m[32][2];
    return modes(m, 32);
}

uint32_t hpcom_IDirect3D9_EnumAdapterModes_c(uint32_t g, uint32_t adapter, uint32_t format, uint32_t index, uint32_t mode)
{
    self(g);
    uint32_t m[32][2];
    uint32_t n = modes(m, 32);
    if (adapter || !is_display(format) || index >= n) return D3DERR_INVALIDCALL;
    wr32(mode, m[index][0]); wr32(mode + 4, m[index][1]); wr32(mode + 8, 60); wr32(mode + 12, format);
    return D3D_OK;
}

uint32_t hpcom_IDirect3D9_GetAdapterDisplayMode_c(uint32_t g, uint32_t adapter, uint32_t mode)
{
    self(g);
    if (adapter) return D3DERR_INVALIDCALL;
    int32_t dw, dh;
    halopad_desktop_size(&dw, &dh);
    wr32(mode, (uint32_t)dw); wr32(mode + 4, (uint32_t)dh); wr32(mode + 8, 60); wr32(mode + 12, F_X8R8G8B8);
    return D3D_OK;
}

uint32_t hpcom_IDirect3D9_CheckDeviceType_c(uint32_t g, uint32_t adapter, uint32_t type, uint32_t display, uint32_t back,
                                             uint32_t windowed)
{
    self(g);
    if (adapter || type != HAL || !is_display(display)) return D3DERR_NOTAVAILABLE;
    if (back == F_UNKNOWN && windowed) return D3D_OK;
    if (display == F_X8R8G8B8 && (back == F_X8R8G8B8 || back == F_A8R8G8B8)) return D3D_OK;
    if (display == F_R5G6B5 && back == F_R5G6B5) return D3D_OK;
    return D3DERR_NOTAVAILABLE;
}

#define U_RENDERTARGET 0x1u
#define U_DEPTHSTENCIL 0x2u
#define U_DYNAMIC 0x200u
#define U_AUTOGENMIPMAP 0x400u
#define U_QUERY_FILTER 0x20000u
#define U_QUERY_POSTPIXELSHADER_BLENDING 0x80000u
#define U_QUERY_ANY 0x3F8000u   /* legacy bump, sRGB read/write, filter, vertex texture, post-PS blending, wrap-and-mip */

uint32_t hpcom_IDirect3D9_CheckDeviceFormat_c(uint32_t g, uint32_t adapter, uint32_t type, uint32_t display, uint32_t usage,
                                               uint32_t rtype, uint32_t format)
{
    self(g);
    if (adapter || type != HAL || !is_display(display)) return D3DERR_NOTAVAILABLE;
    uint32_t known = U_RENDERTARGET | U_DEPTHSTENCIL | U_DYNAMIC | U_AUTOGENMIPMAP | U_QUERY_ANY;
    if (usage & ~known) hp_unsupported("IDirect3D9::CheckDeviceFormat", "usage 0x%x", usage);
    if (usage & U_QUERY_ANY & ~(U_QUERY_FILTER | U_QUERY_POSTPIXELSHADER_BLENDING)) return D3DERR_NOTAVAILABLE;
    int surface = rtype == 1, texture = rtype == 3 || rtype == 5 || rtype == 4;
    if (usage & U_DEPTHSTENCIL) return (surface && is_depth(format)) ? D3D_OK : D3DERR_NOTAVAILABLE;
    if (usage & U_RENDERTARGET) {
        if (!(surface || rtype == 3 || rtype == 5) || !is_rt(format)) return D3DERR_NOTAVAILABLE;
    } else if (!(surface || texture) || !is_texture(format)) {
        return D3DERR_NOTAVAILABLE;
    }
    if ((usage & U_QUERY_POSTPIXELSHADER_BLENDING) && !is_rt(format)) return D3DERR_NOTAVAILABLE;
    if ((usage & U_AUTOGENMIPMAP) && (is_dxt(format) || format == F_V8U8 || format == F_Q8W8V8U8)) return D3DOK_NOAUTOGEN;
    return D3D_OK;
}

uint32_t hpcom_IDirect3D9_CheckDeviceMultiSampleType_c(uint32_t g, uint32_t adapter, uint32_t type, uint32_t format,
                                                        uint32_t windowed, uint32_t ms, uint32_t quality)
{
    (void)windowed; self(g);
    if (adapter || type != HAL || !(is_rt(format) || is_depth(format)) || ms != 0) return D3DERR_NOTAVAILABLE;
    if (quality) wr32(quality, 1);
    return D3D_OK;
}

uint32_t hpcom_IDirect3D9_CheckDepthStencilMatch_c(uint32_t g, uint32_t adapter, uint32_t type, uint32_t display, uint32_t rt,
                                                    uint32_t ds)
{
    self(g);
    return (!adapter && type == HAL && is_display(display) && is_rt(rt) && is_depth(ds)) ? D3D_OK : D3DERR_NOTAVAILABLE;
}

uint32_t hpcom_IDirect3D9_CheckDeviceFormatConversion_c(uint32_t g, uint32_t adapter, uint32_t type, uint32_t src, uint32_t dst)
{
    self(g);
    return (!adapter && type == HAL && src == dst && is_rt(src)) ? D3D_OK : D3DERR_NOTAVAILABLE;
}

void halopad_d3d9_fill_caps(uint32_t c);
uint32_t hpcom_IDirect3D9_GetDeviceCaps_c(uint32_t g, uint32_t adapter, uint32_t type, uint32_t c)
{
    self(g);
    if (adapter || type != HAL) return D3DERR_NOTAVAILABLE;
    halopad_d3d9_fill_caps(c);
    return D3D_OK;
}

/* D3DCAPS9 per docs/GRAPHICS-CONTRACT.md (shared with IDirect3DDevice9::GetDeviceCaps). */
void halopad_d3d9_fill_caps(uint32_t c)
{
    memset(G(c), 0, 304);
    wr32(c + 0, HAL);
    wr32(c + 12, 0x20000u | 0x20000000u | 0x40000000u);            /* Caps2: FULLSCREENGAMMA, DYNAMICTEXTURES, CANAUTOGENMIPMAP */
    wr32(c + 16, 0x20u | 0x100u | 0x200u);                         /* Caps3: alpha flip/discard, copy to vidmem/sysmem */
    wr32(c + 20, 0x80000000u | 1u);                                /* PresentationIntervals: IMMEDIATE, ONE */
    wr32(c + 24, 1u);                                              /* CursorCaps: COLOR */
    wr32(c + 28, 0x10u | 0x20u | 0x40u | 0x80u | 0x100u | 0x200u | 0x400u | 0x800u | 0x1000u | 0x2000u | 0x8000u
                 | 0x10000u | 0x20000u | 0x80000u);                /* DevCaps: memory/draw bits, HW T&L, HW raster; not pure */
    wr32(c + 32, 0x2u | 0x4u | 0x8u | 0x10u | 0x20u | 0x40u | 0x80u | 0x100u | 0x800u | 0x4000u | 0x8000u | 0x10000u
                 | 0x20000u);                                      /* PrimitiveMiscCaps: masks, culling, blend op, clip, independent write masks, separate alpha */
    wr32(c + 36, 0x1u | 0x10u | 0x80u | 0x100u | 0x2000u | 0x10000u | 0x20000u | 0x100000u | 0x200000u | 0x400000u
                 | 0x1000000u | 0x2000000u | 0x4000000u);          /* RasterCaps (see GRAPHICS-CONTRACT.md) */
    wr32(c + 40, 0xFFu);                                           /* ZCmpCaps: all */
    wr32(c + 44, 0x3FFFu);                                         /* SrcBlendCaps */
    wr32(c + 48, 0x3FFFu);                                         /* DestBlendCaps */
    wr32(c + 52, 0xFFu);                                           /* AlphaCmpCaps: all */
    wr32(c + 56, 0x8u | 0x200u | 0x4000u | 0x80000u);              /* ShadeCaps: Gouraud colour/specular/alpha, fog */
    wr32(c + 60, 0x1u | 0x4u | 0x400u | 0x800u | 0x2000u | 0x4000u | 0x8000u | 0x10000u);   /* TextureCaps (no POW2 limits) */
    uint32_t filt = 0x100u | 0x200u | 0x400u | 0x10000u | 0x20000u | 0x1000000u | 0x2000000u | 0x4000000u;
    wr32(c + 64, filt); wr32(c + 68, filt); wr32(c + 72, filt & ~(0x400u | 0x4000000u));
    wr32(c + 76, 0x1u | 0x2u | 0x4u | 0x8u | 0x10u | 0x20u);       /* TextureAddressCaps */
    wr32(c + 80, 0x1u | 0x2u | 0x4u | 0x8u | 0x10u | 0x20u);
    wr32(c + 84, 0x2u | 0x4u | 0x8u | 0x10u | 0x20u);              /* LineCaps */
    wr32(c + 88, 2048); wr32(c + 92, 2048); wr32(c + 96, 2048);    /* MaxTextureWidth/Height, MaxVolumeExtent */
    wr32(c + 100, 2048); wr32(c + 104, 2048); wr32(c + 108, 16);   /* MaxTextureRepeat, MaxTextureAspectRatio, MaxAnisotropy */
    float maxw = 1e10f, band = 1e8f;                               /* MaxVertexW, guard band */
    memcpy(G(c + 112), &maxw, 4);
    float gl = -band, gr = band;
    memcpy(G(c + 116), &gl, 4); memcpy(G(c + 120), &gl, 4); memcpy(G(c + 124), &gr, 4); memcpy(G(c + 128), &gr, 4);
    wr32(c + 136, 0xFFu | 0x100u);                                 /* StencilCaps: all ops, two-sided */
    wr32(c + 140, 8u | 0x80000u);                                  /* FVFCaps: 8 texture coordinate sets, PSIZE */
    wr32(c + 144, 0x03FFFFFFu);                                    /* TextureOpCaps: all */
    wr32(c + 148, 8); wr32(c + 152, 8);                            /* MaxTextureBlendStages, MaxSimultaneousTextures */
    wr32(c + 156, 0x1u | 0x2u | 0x8u | 0x10u | 0x20u);             /* VertexProcessingCaps: texgen, material source, directional/positional lights, local viewer */
    wr32(c + 160, 8); wr32(c + 164, 6); wr32(c + 168, 4); wr32(c + 172, 0);
    float psize = 256.0f;
    memcpy(G(c + 176), &psize, 4);
    wr32(c + 180, 0x00FFFFFFu); wr32(c + 184, 0x00FFFFFFu);        /* MaxPrimitiveCount, MaxVertexIndex */
    wr32(c + 188, 16); wr32(c + 192, 255);                         /* MaxStreams, MaxStreamStride */
    wr32(c + 196, 0xFFFE0200u); wr32(c + 200, 256);                /* VertexShaderVersion 2.0, MaxVertexShaderConst */
    wr32(c + 204, 0xFFFF0200u);                                    /* PixelShaderVersion 2.0 */
    float ps1max = 8.0f;
    memcpy(G(c + 208), &ps1max, 4);
    wr32(c + 212, 0x2u | 0x10u);                                   /* DevCaps2: stream offset, vertex elements can share stream offset */
    wr32(c + 232, 1);                                              /* NumberOfAdaptersInGroup */
    wr32(c + 236, 0x1u | 0x2u | 0x4u | 0x8u | 0x10u | 0x20u | 0x40u | 0x80u);   /* DeclTypes */
    wr32(c + 240, 1);                                              /* NumSimultaneousRTs */
    wr32(c + 244, 0x100u | 0x200u | 0x1000000u | 0x2000000u);      /* StretchRectFilterCaps: point/linear */
    /* VS20Caps (D3DVSHADERCAPS2_0 at 248): Caps, DynamicFlowControlDepth, NumTemps, StaticFlowControlDepth */
    wr32(c + 248, 0); wr32(c + 252, 0); wr32(c + 256, 12); wr32(c + 260, 1);
    /* PS20Caps (D3DPSHADERCAPS2_0 at 264): Caps, DynamicFlowControlDepth, NumTemps, StaticFlowControlDepth, NumInstructionSlots */
    wr32(c + 264, 0); wr32(c + 268, 0); wr32(c + 272, 12); wr32(c + 276, 0); wr32(c + 280, 96);
    wr32(c + 284, 0);                                              /* VertexTextureFilterCaps: no vertex textures */
    wr32(c + 288, 65535); wr32(c + 292, 96);                       /* MaxVShaderInstructionsExecuted, MaxPShaderInstructionsExecuted */
    wr32(c + 296, 0); wr32(c + 300, 0);                            /* no shader model 3 */
}

uint32_t hpcom_IDirect3D9_GetAdapterMonitor_c(uint32_t g, uint32_t adapter)
{
    self(g);
    return adapter ? 0 : 0x00010001u;                              /* the single monitor's HMONITOR */
}

uint32_t halopad_d3d9_create_device(uint32_t d3d, uint32_t adapter, uint32_t type, uint32_t focus, uint32_t behavior,
                                    uint32_t pp, uint32_t out);
uint32_t hpcom_IDirect3D9_CreateDevice_c(uint32_t g, uint32_t adapter, uint32_t type, uint32_t focus, uint32_t behavior,
                                          uint32_t pp, uint32_t out)
{
    self(g);
    return halopad_d3d9_create_device(g, adapter, type, focus, behavior, pp, out);
}

/* DebugSetMute: the retail d3d9.dll's debug-output switch, which D3DX looks up at run time. The
   retail runtime prints nothing, so there is nothing to mute (a stdcall function with no arguments). */
uint32_t DebugSetMute_c(void) { return 0; }
