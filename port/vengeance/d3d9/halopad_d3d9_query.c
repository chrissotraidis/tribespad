/* HaloPad Direct3D 9, part 6 (G4): queries.
 *
 * Halo creates 1,024 occlusion queries at start-up (0x53a260; D3DERR_NOTAVAILABLE would
 * turn them off) and uses them as Issue(BEGIN), draws, Issue(END), then spins on
 * GetData(&count, 4, D3DGETDATA_FLUSH) while it returns S_FALSE (0x53ae20). Occlusion
 * counts come from Metal's visibility counters; if the counted draws are still in the
 * frame being recorded, GetData submits the frame and waits, so the answer is always
 * ready at the first call. Other query types stop with their number. */
#include "halopad_win32.h"

void *halopad_com_state(const char *iface, uint32_t g);
uint32_t halopad_com_new(const char *iface, uint32_t size, void *state, void (*destroy)(void *));
uint32_t halopad_com_addref(uint32_t g);
uint32_t halopad_com_release(uint32_t g);
void halopad_metal_visibility_begin(void *target);
void halopad_metal_visibility_end(void *target, uint32_t *first, uint32_t *last, uint64_t *gen);
uint64_t halopad_metal_visibility_read(void *target, uint32_t first, uint32_t last, uint64_t gen);

#define D3D_OK 0u
#define D3DERR_INVALIDCALL 0x8876086Cu
#include "halopad_d3d9_internal.h"

enum { Q_NEW, Q_BUILDING, Q_ISSUED, Q_EMPTY };
typedef struct { uint32_t guest, device, type, state, first, last; uint64_t gen; } query;

static uint32_t counting;                  /* the occlusion query now counting, if any */

static query *Q(uint32_t g) { return halopad_com_state("IDirect3DQuery9", g); }
static device *dev(uint32_t g) { return halopad_com_state("IDirect3DDevice9", g); }

static void stop(query *q)
{
    if (counting != q->guest) return;
    halopad_metal_visibility_end(dev(q->device)->target, &q->first, &q->last, &q->gen);
    counting = 0;
}

static void q_destroy(void *p)
{
    query *q = p;
    if (counting == q->guest) { uint32_t f, l; uint64_t g; halopad_metal_visibility_end(dev(q->device)->target, &f, &l, &g); counting = 0; }
    free(q);
}

uint32_t hpcom_IDirect3DDevice9_CreateQuery_c(uint32_t g, uint32_t type, uint32_t out)
{
    dev(g);
    if (type != 9) hp_unsupported("IDirect3DDevice9::CreateQuery", "query type %u", type);   /* 9: OCCLUSION */
    if (!out) return D3D_OK;                                        /* a support check */
    query *q = calloc(1, sizeof *q);
    q->device = g; q->type = type;
    q->guest = halopad_com_new("IDirect3DQuery9", 4, q, q_destroy);
    wr32(out, q->guest);
    return D3D_OK;
}

uint32_t hpcom_IDirect3DQuery9_QueryInterface_c(uint32_t g, uint32_t iid, uint32_t out)
{
    (void)out; Q(g);
    hp_unsupported("IDirect3DQuery9", "QueryInterface for %08x-...", rd32(iid));
}
uint32_t hpcom_IDirect3DQuery9_AddRef_c(uint32_t g) { Q(g); return halopad_com_addref(g); }
uint32_t hpcom_IDirect3DQuery9_Release_c(uint32_t g) { Q(g); return halopad_com_release(g); }
uint32_t hpcom_IDirect3DQuery9_GetDevice_c(uint32_t g, uint32_t out)
{
    query *q = Q(g);
    halopad_com_addref(q->device);
    wr32(out, q->device);
    return D3D_OK;
}
uint32_t hpcom_IDirect3DQuery9_GetType_c(uint32_t g) { return Q(g)->type; }
uint32_t hpcom_IDirect3DQuery9_GetDataSize_c(uint32_t g) { Q(g); return 4; }   /* a DWORD sample count */

uint32_t hpcom_IDirect3DQuery9_Issue_c(uint32_t g, uint32_t flags)
{
    query *q = Q(g);
    if (flags == 2) {                                               /* D3DISSUE_BEGIN (again: restart) */
        stop(q);
        halopad_metal_visibility_begin(dev(q->device)->target);
        counting = q->guest;
        q->state = Q_BUILDING;
        return D3D_OK;
    }
    if (flags == 1) {                                               /* D3DISSUE_END */
        if (q->state == Q_BUILDING) { stop(q); q->state = Q_ISSUED; }
        else q->state = Q_EMPTY;                                    /* END alone: nothing drawn, count 0 */
        return D3D_OK;
    }
    return D3DERR_INVALIDCALL;
}

uint32_t hpcom_IDirect3DQuery9_GetData_c(uint32_t g, uint32_t data, uint32_t size, uint32_t flags)
{
    query *q = Q(g);
    if (flags & ~1u) return D3DERR_INVALIDCALL;                     /* D3DGETDATA_FLUSH */
    if (q->state == Q_BUILDING) return D3DERR_INVALIDCALL;
    if (q->state == Q_NEW) hp_unsupported("IDirect3DQuery9::GetData", "a query that was never issued");
    if (data && size < 4) return D3DERR_INVALIDCALL;
    uint64_t n = q->state == Q_EMPTY ? 0 : halopad_metal_visibility_read(dev(q->device)->target, q->first, q->last, q->gen);
    q->gen = 0;                                                     /* read once submitted */
    if (data) wr32(data, n > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)n);
    return D3D_OK;
}
