/* HaloPad COM objects (G3): vtables and object bookkeeping for interfaces HaloPad
 * implements natively (Direct3D 9, later DirectInput 8 and DirectSound 8).
 *
 * A COM object handed to Halo is guest memory whose first word points to the
 * interface's vtable: an array of guest addresses (scripts/va-model.py) that dispatch to
 * hpcom_<Interface>_<Method>, HaloPad's implementation or a stub naming the method.
 * Vtables are built once and are read-only. Reference counts and native state live in
 * a host-side pool, found through a hash table keyed by the object's guest address. */
#include "halopad_win32.h"
#include <pthread.h>

extern const uint32_t halopad_com_base, halopad_com_interface_count;
extern const uint32_t halopad_com_first[], halopad_com_count[];
extern const char *const halopad_com_interfaces[];

typedef struct { uint32_t guest; int iface; uint32_t refs, binds; void *state; void (*destroy)(void *); } comobj;
#define MAXOBJ 65536u
static comobj objects[MAXOBJ];
static uint32_t free_list[MAXOBJ], nfree, used;          /* pool slots */
#define HSIZE 131072u                                     /* power of two, twice MAXOBJ */
#define TOMB 0xFFFFFFFFu
static uint32_t slot_of[HSIZE];                           /* 0 empty, TOMB deleted, else pool index + 1 */

static uint32_t hash(uint32_t g) { return (g * 2654435761u) >> 15 & (HSIZE - 1); }

static uint32_t filled;                                   /* live entries and tombstones */

static void index_add(uint32_t g, uint32_t idx)
{
    if (filled + 1 > HSIZE / 4 * 3) {                     /* too many tombstones: rebuild from the pool */
        memset(slot_of, 0, sizeof slot_of);
        filled = 0;
        for (uint32_t i = 0; i < used; i++) {
            if (!objects[i].guest || i == idx) continue;
            uint32_t h = hash(objects[i].guest);
            while (slot_of[h]) h = (h + 1) & (HSIZE - 1);
            slot_of[h] = i + 1;
            filled++;
        }
    }
    for (uint32_t h = hash(g);; h = (h + 1) & (HSIZE - 1)) {
        if (!slot_of[h]) { slot_of[h] = idx + 1; filled++; return; }
        if (slot_of[h] == TOMB) { slot_of[h] = idx + 1; return; }
    }
}

static uint32_t *index_find(uint32_t g)
{
    for (uint32_t h = hash(g);; h = (h + 1) & (HSIZE - 1)) {
        if (!slot_of[h]) return NULL;
        if (slot_of[h] != TOMB && objects[slot_of[h] - 1].guest == g) return &slot_of[h];
    }
}
static uint32_t vtables[64];

uint32_t halopad_com_vtable(const char *iface);
uint32_t halopad_com_new(const char *iface, uint32_t size, void *state, void (*destroy)(void *));
void *halopad_com_state(const char *iface, uint32_t g);
uint32_t halopad_com_addref(uint32_t g);
uint32_t halopad_com_release(uint32_t g);
void halopad_com_bind(uint32_t g);
void halopad_com_unbind(uint32_t g);
const char *halopad_com_interface(uint32_t g);

static int iface_index(const char *iface)
{
    for (uint32_t i = 0; i < halopad_com_interface_count; i++)
        if (!strcmp(halopad_com_interfaces[i], iface)) return (int)i;
    hp_unsupported("COM", "interface %s missing from config/runtime/com-interfaces.txt", iface);
}

static uint32_t halopad_com_vtable_unlocked(const char *iface)
{
    int i = iface_index(iface);
    if (!vtables[i]) {
        uint32_t n = halopad_com_count[i];
        uint32_t vt = halopad_heap_alloc(4 * n, 0);
        for (uint32_t k = 0; k < n; k++) wr32(vt + 4 * k, halopad_com_base + 16 * (halopad_com_first[i] + k));
        vtables[i] = vt;
    }
    return vtables[i];
}

/* New object: 'size' guest bytes (at least 4, for the vtable pointer), reference count 1. */
static uint32_t halopad_com_new_unlocked(const char *iface, uint32_t size, void *state, void (*destroy)(void *))
{
    uint32_t g = halopad_heap_alloc(size < 4 ? 4 : size, 1);
    wr32(g, halopad_com_vtable(iface));
    uint32_t i;
    if (nfree) i = free_list[--nfree];
    else if (used < MAXOBJ) i = used++;
    else hp_unsupported("COM", "more than %u live objects", MAXOBJ);
    objects[i] = (comobj){g, iface_index(iface), 1, 0, state, destroy};
    index_add(g, i);
    return g;
}

static comobj *find(const char *what, uint32_t g)
{
    uint32_t *s = g ? index_find(g) : NULL;
    if (!s) hp_unsupported(what, "object 0x%08x that HaloPad did not create", g);
    return &objects[*s - 1];
}

/* Native state of an object, checking its interface. */
static void *halopad_com_state_unlocked(const char *iface, uint32_t g)
{
    comobj *o = find(iface, g);
    if (strcmp(halopad_com_interfaces[o->iface], iface))
        hp_unsupported(iface, "object 0x%08x is a %s", g, halopad_com_interfaces[o->iface]);
    return o->state;
}

static uint32_t halopad_com_addref_unlocked(uint32_t g) { return ++find("AddRef", g)->refs; }

static void destroy_if_unused(comobj *o)
{
    if (o->refs || o->binds) return;
    if (o->destroy) o->destroy(o->state);
    uint32_t *s = index_find(o->guest);
    *s = TOMB;
    halopad_heap_free(o->guest);
    *o = (comobj){0};
    free_list[nfree++] = (uint32_t)(o - objects);
}

/* Public reference count, as Direct3D reports it. An object the device still has bound
   (a texture, stream, index buffer, declaration or shader) survives its last Release
   until it is unbound, as in Direct3D 9, whose device keeps internal references. */
static uint32_t halopad_com_release_unlocked(uint32_t g)
{
    comobj *o = find("Release", g);
    if (!o->refs) hp_unsupported("Release", "object 0x%08x whose reference count is already 0", g);
    uint32_t n = --o->refs;
    destroy_if_unused(o);
    return n;
}

static void halopad_com_bind_unlocked(uint32_t g) { if (g) find("bind", g)->binds++; }
static void halopad_com_unbind_unlocked(uint32_t g)
{
    if (!g) return;
    comobj *o = find("unbind", g);
    o->binds--;
    destroy_if_unused(o);
}

/* The interface name of a live object (resource type checks). */
static const char *halopad_com_interface_unlocked(uint32_t g) { return halopad_com_interfaces[find("interface", g)->iface]; }

/* ---- thread safety: Direct3D, DirectInput and DirectSound calls come from several
   threads; every entry point holds one recursive lock (destroy callbacks run under it) ---- */
static pthread_mutex_t com_lock;
__attribute__((constructor)) static void com_lock_init(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&com_lock, &a);
}
uint32_t halopad_com_vtable(const char *iface) { pthread_mutex_lock(&com_lock); uint32_t r = halopad_com_vtable_unlocked(iface); pthread_mutex_unlock(&com_lock); return r; }
uint32_t halopad_com_new(const char *iface, uint32_t size, void *state, void (*destroy)(void *)) { pthread_mutex_lock(&com_lock); uint32_t r = halopad_com_new_unlocked(iface, size, state, destroy); pthread_mutex_unlock(&com_lock); return r; }
void *halopad_com_state(const char *iface, uint32_t g) { pthread_mutex_lock(&com_lock); void *r = halopad_com_state_unlocked(iface, g); pthread_mutex_unlock(&com_lock); return r; }
uint32_t halopad_com_addref(uint32_t g) { pthread_mutex_lock(&com_lock); uint32_t r = halopad_com_addref_unlocked(g); pthread_mutex_unlock(&com_lock); return r; }
uint32_t halopad_com_release(uint32_t g) { pthread_mutex_lock(&com_lock); uint32_t r = halopad_com_release_unlocked(g); pthread_mutex_unlock(&com_lock); return r; }
void halopad_com_bind(uint32_t g) { pthread_mutex_lock(&com_lock); halopad_com_bind_unlocked(g); pthread_mutex_unlock(&com_lock); }
void halopad_com_unbind(uint32_t g) { pthread_mutex_lock(&com_lock); halopad_com_unbind_unlocked(g); pthread_mutex_unlock(&com_lock); }
const char *halopad_com_interface(uint32_t g) { pthread_mutex_lock(&com_lock); const char *r = halopad_com_interface_unlocked(g); pthread_mutex_unlock(&com_lock); return r; }
