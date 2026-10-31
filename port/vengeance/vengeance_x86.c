/* x86 helpers Tribes: Vengeance needs beyond Starsiege's: the time-stamp
 * counter and the counting part of repe/repne cmpsw/cmpsd, as in the pinned
 * HaloPad support (ref/halopad/port/llasm-support/llasm_halopad.c). Unreal
 * measures rdtsc against QueryPerformanceCounter at start-up (appInitTiming),
 * so any steady rate works; this one is 2.4 GHz from the monotonic clock. */
#include <stdint.h>
#include <time.h>
#include "../llasm-support/llasm_cpu.h"

EXTERNC void CCALL x86_rdtsc(CPU)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    uint64_t ns = (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
    uint64_t v = ns / 5 * 12 + ns % 5 * 12 / 5;
    eax = (uint32_t)v;
    edx = (uint32_t)(v >> 32);
}

/* Called with ecx >= 1: skip the elements the repeated compare passes over,
   stopping before the last one it compares; the translated code then performs
   that compare itself, which sets the flags exactly as x86 does. */
#define PREP(name, type, step, cond) \
EXTERNC void CCALL name(CPU) \
{ \
    int32_t dir = (eflags & DF) ? -(step) : (step); \
    while (ecx > 1 && (*(type *)REG2PTR(esi) cond *(type *)REG2PTR(edi))) \
    { \
        esi += dir; edi += dir; ecx--; \
    } \
}
PREP(x86_repe_cmpsw_prep, uint16_t, 2, ==)
PREP(x86_repne_cmpsw_prep, uint16_t, 2, !=)
PREP(x86_repe_cmpsd_prep, uint32_t, 4, ==)
PREP(x86_repne_cmpsd_prep, uint32_t, 4, !=)
