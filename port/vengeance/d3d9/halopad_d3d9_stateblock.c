/* HaloPad Direct3D 9, part 7 (G3): state blocks (IDirect3DStateBlock9).
 *
 * Keystone.dll (Halo's chat UI, translated) saves and restores Halo's device state around
 * its own drawing with state blocks. As in Direct3D 9:
 *   - between BeginStateBlock and EndStateBlock the recordable setters (render, texture-stage
 *     and sampler states, transforms, viewport, textures, streams, indices, declaration/FVF,
 *     shaders and their constants, material, lights) are recorded into the block and do not
 *     change the device; getters keep reading the device;
 *   - CreateStateBlock(D3DSBT_ALL / PIXELSTATE / VERTEXSTATE) captures the current values of
 *     the documented set for that type;
 *   - Capture refreshes the block's values from the device, Apply sets them on the device;
 *   - bound objects (textures, buffers, declarations, shaders) held by a block stay alive
 *     until the block is released.
 * Setter recording lives in halopad_d3d9_device.c (W(d)/MARK). */
#include "halopad_win32.h"

void *halopad_com_state(const char *iface, uint32_t g);
uint32_t halopad_com_new(const char *iface, uint32_t size, void *state, void (*destroy)(void *));
uint32_t halopad_com_addref(uint32_t g);
uint32_t halopad_com_release(uint32_t g);
void halopad_com_bind(uint32_t g);
void halopad_com_unbind(uint32_t g);

#define D3D_OK 0u
#define D3DERR_INVALIDCALL 0x8876086Cu
#include "halopad_d3d9_internal.h"

int halopad_d3d9_rs_valid(uint32_t s);
int halopad_d3d9_tss_valid(uint32_t s);
int halopad_d3d9_light_slot(device *d, uint32_t index, int create);

static device *dev(uint32_t g) { return halopad_com_state("IDirect3DDevice9", g); }
static stateblock *SB(uint32_t g) { return halopad_com_state("IDirect3DStateBlock9", g); }

static void rebind(uint32_t *slot, uint32_t g)
{
    if (*slot == g) return;
    halopad_com_bind(g);
    halopad_com_unbind(*slot);
    *slot = g;
}

/* Direct3D 9 SDK, "State Blocks Save and Restore State": the states each block type holds */
static const uint8_t pixel_rs[] = {7, 8, 9, 14, 15, 16, 19, 20, 23, 24, 25, 26, 27, 36, 37, 38, 52, 53, 54, 55, 56, 57, 58, 59, 60,
                                   128, 129, 130, 131, 132, 133, 134, 135, 168, 171, 174, 175, 176, 185, 186, 187, 188, 189,
                                   190, 191, 192, 193, 194, 195, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209};
static const uint8_t vertex_rs[] = {9, 22, 28, 29, 34, 35, 36, 37, 38, 48, 136, 137, 139, 140, 141, 142, 143, 145, 146, 147,
                                    148, 151, 152, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 166, 167, 170, 172, 173,
                                    178, 179, 180, 181, 182, 183, 184};
static const uint8_t pixel_tss[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 22, 23, 24, 26, 27, 28, 32};
static const uint8_t vertex_tss[] = {11, 24};
static const uint8_t pixel_ss[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
static const uint8_t vertex_ss[] = {13};

static int transform_valid(int i) { return i == 2 || i == 3 || (i >= 16 && i <= 23) || (i >= 256 && i < 512); }

static void mark_type(stateblock *b, device *d, uint32_t type)
{
    int all = type == 1, pixel = all || type == 2, vertex = all || type == 3;
    for (uint32_t s = 0; s < NRS; s++) if (all && halopad_d3d9_rs_valid(s)) b->m.rs[s] = 1;
    if (!all) {
        if (pixel) for (size_t k = 0; k < sizeof pixel_rs; k++) if (pixel_rs[k] < NRS && halopad_d3d9_rs_valid(pixel_rs[k])) b->m.rs[pixel_rs[k]] = 1;
        if (vertex) for (size_t k = 0; k < sizeof vertex_rs; k++) if (vertex_rs[k] < NRS && halopad_d3d9_rs_valid(vertex_rs[k])) b->m.rs[vertex_rs[k]] = 1;
    }
    for (int st = 0; st < 8; st++) {
        if (pixel) for (size_t k = 0; k < sizeof pixel_tss; k++) b->m.tss[st][pixel_tss[k]] = 1;
        if (vertex) for (size_t k = 0; k < sizeof vertex_tss; k++) b->m.tss[st][vertex_tss[k]] = 1;
    }
    for (int sm = 0; sm < NSAMPLERS; sm++) {
        if (pixel) for (size_t k = 0; k < sizeof pixel_ss; k++) b->m.ss[sm][pixel_ss[k]] = 1;
        if (vertex) for (size_t k = 0; k < sizeof vertex_ss; k++) b->m.ss[sm][vertex_ss[k]] = 1;
    }
    if (all) {
        for (int i = 0; i < 512; i++) b->m.transform[i] = (uint8_t)transform_valid(i);
        b->m.viewport = b->m.indices = b->m.material = 1;
        memset(b->m.texture, 1, sizeof b->m.texture);
        memset(b->m.stream, 1, sizeof b->m.stream);
    }
    if (pixel) { b->m.ps = 1; memset(b->m.psf, 1, sizeof b->m.psf); memset(b->m.psi, 1, sizeof b->m.psi); memset(b->m.psb, 1, sizeof b->m.psb); }
    if (vertex) {
        b->m.vs = b->m.layout = 1;
        memset(b->m.vsf, 1, sizeof b->m.vsf); memset(b->m.vsi, 1, sizeof b->m.vsi); memset(b->m.vsb, 1, sizeof b->m.vsb);
        /* the lights the device has now */
        b->st.nlight = d->nlight;
        for (uint32_t i = 0; i < d->nlight; i++) {
            b->st.light[i].index = d->light[i].index;
            b->m.light[i] = b->m.light_enable[i] = 1;
        }
    }
}

/* copy the marked state between a device-shaped 'from' and 'to' (bindings rebound) */
static void transfer(stateblock *b, device *from, device *to, int to_device)
{
    for (uint32_t s = 0; s < NRS; s++) if (b->m.rs[s]) to->rs[s] = from->rs[s];
    for (int st = 0; st < 8; st++) for (int s = 0; s < NTSS; s++) if (b->m.tss[st][s]) to->tss[st][s] = from->tss[st][s];
    for (int sm = 0; sm < NSAMPLERS; sm++) for (int s = 0; s < NSS; s++) if (b->m.ss[sm][s]) to->ss[sm][s] = from->ss[sm][s];
    for (int i = 0; i < 512; i++) if (b->m.transform[i]) memcpy(to->transform[i], from->transform[i], 64);
    if (b->m.viewport) memcpy(to->viewport, from->viewport, sizeof to->viewport);
    for (int i = 0; i < 16; i++) if (b->m.texture[i]) rebind(&to->texture[i], from->texture[i]);
    for (int i = 0; i < 16; i++) if (b->m.stream[i]) {
        rebind(&to->stream[i], from->stream[i]);
        to->stream_offset[i] = from->stream_offset[i];
        to->stream_stride[i] = from->stream_stride[i];
    }
    if (b->m.indices) rebind(&to->indices, from->indices);
    if (b->m.layout) { rebind(&to->decl, from->decl); to->fvf = from->fvf; }
    if (b->m.vs) rebind(&to->vs, from->vs);
    if (b->m.ps) rebind(&to->ps, from->ps);
    for (int i = 0; i < 256; i++) if (b->m.vsf[i]) memcpy(to->vsf[i], from->vsf[i], 16);
    for (int i = 0; i < 224; i++) if (b->m.psf[i]) memcpy(to->psf[i], from->psf[i], 16);
    for (int i = 0; i < 16; i++) {
        if (b->m.vsi[i]) memcpy(to->vsi[i], from->vsi[i], 16);
        if (b->m.psi[i]) memcpy(to->psi[i], from->psi[i], 16);
        if (b->m.vsb[i]) to->vsb[i] = from->vsb[i];
        if (b->m.psb[i]) to->psb[i] = from->psb[i];
    }
    if (b->m.material) memcpy(to->material, from->material, sizeof to->material);
    /* lights by index: the block's slots name the indices it holds */
    for (uint32_t i = 0; i < b->st.nlight; i++) {
        if (!b->m.light[i] && !b->m.light_enable[i]) continue;
        uint32_t index = b->st.light[i].index;
        int fs = to_device ? (int)i : halopad_d3d9_light_slot(from, index, 0);
        int ts = to_device ? halopad_d3d9_light_slot(to, index, 1) : (int)i;
        if (fs < 0) continue;                                        /* capture: the device has no such light */
        if (b->m.light[i] && from->light[fs].set) { memcpy(to->light[ts].light, from->light[fs].light, sizeof to->light[ts].light); to->light[ts].set = 1; }
        if (b->m.light_enable[i]) to->light[ts].enabled = from->light[fs].enabled;
    }
}

static struct { uint32_t dev, n; } live_blocks[16];
static void count_block(uint32_t dev, int delta)
{
    for (int k = 0; k < 16; k++) if (live_blocks[k].dev == dev && (live_blocks[k].n || delta > 0)) { live_blocks[k].n += (uint32_t)delta; return; }
    for (int k = 0; k < 16; k++) if (!live_blocks[k].n && delta > 0) { live_blocks[k].dev = dev; live_blocks[k].n = 1; return; }
    hp_unsupported("Direct3D 9", "state blocks on more than 16 devices");
}
uint32_t halopad_d3d9_live_stateblocks(uint32_t dev)
{
    for (int k = 0; k < 16; k++) if (live_blocks[k].dev == dev) return live_blocks[k].n;
    return 0;
}

static void sb_destroy(void *p)
{
    count_block(((stateblock *)p)->device, -1);
    stateblock *b = p;
    for (int i = 0; i < 16; i++) { rebind(&b->st.texture[i], 0); rebind(&b->st.stream[i], 0); }
    rebind(&b->st.indices, 0); rebind(&b->st.decl, 0); rebind(&b->st.vs, 0); rebind(&b->st.ps, 0);
    free(b);
}

static uint32_t publish(stateblock *b, uint32_t out)
{
    count_block(b->device, 1);
    b->guest = halopad_com_new("IDirect3DStateBlock9", 4, b, sb_destroy);
    wr32(out, b->guest);
    return D3D_OK;
}

uint32_t hpcom_IDirect3DDevice9_BeginStateBlock_c(uint32_t g)
{
    device *d = dev(g);
    if (d->rec) return D3DERR_INVALIDCALL;
    stateblock *b = calloc(1, sizeof *b);
    b->device = g;
    d->rec = b;
    return D3D_OK;
}

uint32_t hpcom_IDirect3DDevice9_EndStateBlock_c(uint32_t g, uint32_t out)
{
    device *d = dev(g);
    if (!d->rec || !out) return D3DERR_INVALIDCALL;
    stateblock *b = d->rec;
    d->rec = NULL;
    return publish(b, out);
}

uint32_t hpcom_IDirect3DDevice9_CreateStateBlock_c(uint32_t g, uint32_t type, uint32_t out)
{
    device *d = dev(g);
    if (type < 1 || type > 3 || !out) return D3DERR_INVALIDCALL;
    if (d->rec) return D3DERR_INVALIDCALL;
    stateblock *b = calloc(1, sizeof *b);
    b->device = g;
    mark_type(b, d, type);
    transfer(b, d, &b->st, 0);
    return publish(b, out);
}

uint32_t hpcom_IDirect3DStateBlock9_QueryInterface_c(uint32_t g, uint32_t iid, uint32_t out)
{
    (void)out; SB(g);
    hp_unsupported("IDirect3DStateBlock9", "QueryInterface for %08x-...", rd32(iid));
}
uint32_t hpcom_IDirect3DStateBlock9_AddRef_c(uint32_t g) { SB(g); return halopad_com_addref(g); }
uint32_t hpcom_IDirect3DStateBlock9_Release_c(uint32_t g) { SB(g); return halopad_com_release(g); }
uint32_t hpcom_IDirect3DStateBlock9_GetDevice_c(uint32_t g, uint32_t out)
{
    stateblock *b = SB(g);
    halopad_com_addref(b->device);
    wr32(out, b->device);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DStateBlock9_Capture_c(uint32_t g)
{
    stateblock *b = SB(g);
    device *d = dev(b->device);
    if (d->rec) hp_unsupported("IDirect3DStateBlock9::Capture", "while another block is recording");
    transfer(b, d, &b->st, 0);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DStateBlock9_Apply_c(uint32_t g)
{
    stateblock *b = SB(g);
    device *d = dev(b->device);
    if (d->rec) hp_unsupported("IDirect3DStateBlock9::Apply", "while another block is recording");
    transfer(b, &b->st, d, 1);
    return D3D_OK;
}
