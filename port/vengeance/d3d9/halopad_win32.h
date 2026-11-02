/* HaloPad Windows runtime: shared helpers for services called from translated Halo code.
 * Services take guest addresses as uint32_t and convert them explicitly, so a guest
 * NULL stays NULL. Anything a service does not implement stops through hp_unsupported
 * with the service name and the values that were not handled. */
#ifndef HALOPAD_WIN32_H
#define HALOPAD_WIN32_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *halopad_guest_ptr(uint32_t guest);
uint32_t halopad_guest_commit(uint32_t guest, uint32_t size);
extern _Thread_local uint32_t halopad_last_error;   /* per thread, as GetLastError is */

static inline void *G(uint32_t guest) { return guest ? halopad_guest_ptr(guest) : NULL; }
static inline uint32_t rd32(uint32_t guest) { uint32_t v; memcpy(&v, halopad_guest_ptr(guest), 4); return v; }
static inline void wr32(uint32_t guest, uint32_t v) { memcpy(halopad_guest_ptr(guest), &v, 4); }
static inline void wr16(uint32_t guest, uint16_t v) { memcpy(halopad_guest_ptr(guest), &v, 2); }

void halopad_trace_guest_stack(void);   /* halopad_guest.c: guest registers and the top of the stack */
/* HALOPAD_TRACE_FAILURES=1: services report failures they return (diagnostics only) */
static inline int hp_trace_failures(void) { static int v = -1; if (v < 0) v = getenv("HALOPAD_TRACE_FAILURES") != NULL; return v; }
#define HP_TRACE_FAIL(fmt, ...) do { if (hp_trace_failures()) fprintf(stderr, "HALOPAD FAILURE: " fmt "\n", __VA_ARGS__); } while (0)
__attribute__((noreturn, format(printf, 2, 3)))
static inline void hp_unsupported(const char *service, const char *fmt, ...)
{
    fprintf(stderr, "HALOPAD TRAP: %s: ", service);
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    __builtin_va_end(ap);
    fprintf(stderr, " is not supported by the runtime yet\n");
    halopad_trace_guest_stack();
    abort();
}

/* Guest virtual memory (halopad_vmem.c). */
uint32_t halopad_vm_reserve(uint32_t address, uint32_t size, int commit_now);
int halopad_vm_release(uint32_t address);
/* Guest heap for runtime-owned data (halopad_heap.c). */
uint32_t halopad_heap_alloc(uint32_t size, int zero);
void halopad_heap_free(uint32_t address);

/* Windows constants used by several services. */
#define HP_ERROR_INVALID_HANDLE 6
#define HP_ERROR_NOT_ENOUGH_MEMORY 8
#define HP_ERROR_INVALID_PARAMETER 87
#define HP_ERROR_INSUFFICIENT_BUFFER 122
#define HP_INSTALL_DIR "C:\\Program Files\\Microsoft Games\\Halo Custom Edition"
#define HP_IMAGE_BASE 0x00400000u

/* COM methods shared across interfaces: HPCOM_FWDn(Interface, Method, impl) defines
 * hpcom_<Interface>_<Method>_c taking 'this' and n more arguments and calling
 * impl("Interface", this, ...). scripts/gen-com-wrappers.py generates their wrappers. */
#define HPCOM_FWD0(I, M, impl) uint32_t hpcom_##I##_##M##_c(uint32_t g) { return impl(#I, g); }
#define HPCOM_FWD1(I, M, impl) uint32_t hpcom_##I##_##M##_c(uint32_t g, uint32_t a) { return impl(#I, g, a); }
#define HPCOM_FWD2(I, M, impl) uint32_t hpcom_##I##_##M##_c(uint32_t g, uint32_t a, uint32_t b) { return impl(#I, g, a, b); }
#define HPCOM_FWD3(I, M, impl) uint32_t hpcom_##I##_##M##_c(uint32_t g, uint32_t a, uint32_t b, uint32_t c) { return impl(#I, g, a, b, c); }
#define HPCOM_FWD4(I, M, impl) uint32_t hpcom_##I##_##M##_c(uint32_t g, uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return impl(#I, g, a, b, c, d); }
#endif
