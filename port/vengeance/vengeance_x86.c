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

/* FNSAVE / FRSTOR over TribesPad's extended x87 model (physical register i: the
   64-bit significand in _st[i], sign and exponent in _st_exp[i]; two tag bits per
   physical register in _st_tag). Layout: the 108-byte 32-bit protected-mode
   image (Intel SDM Vol. 1, 8.1.10 and Figure 8-9): control, status (with TOP)
   and tag words; instruction and operand pointers (recorded as zero: TribesPad
   does not track the last x87 instruction); ST(0)..ST(7) in stack order, ten
   bytes each. FNSAVE then initializes the FPU, as FNINIT does. */
#include <stdlib.h>
#include <string.h>
extern void x87_fninit_void(_cpu *);
EXTERNC void CCALL x87_fnsave_void(CPU, uint8_t *state)
{
    if (!cpu->_st_extended || cpu->_st_top >= 8) abort();
    uint32_t words[7] = {cpu->_st_cw & 0xffff, (cpu->_st_sw_cond & ~0x3800u & 0xffff) | (cpu->_st_top << 11), cpu->_st_tag & 0xffff, 0, 0, 0, 0};
    memcpy(state, words, sizeof words);
    for (unsigned n = 0; n < 8; n++) {
        unsigned i = (cpu->_st_top + n) & 7;
        memcpy(state + 28 + 10 * n, &cpu->_st[i], 8);
        memcpy(state + 36 + 10 * n, &cpu->_st_exp[i], 2);
    }
    x87_fninit_void(cpu);
}
EXTERNC void CCALL x87_frstor_void(CPU, const uint8_t *state)
{
    if (!cpu->_st_extended) abort();
    uint32_t words[3];memcpy(words, state, sizeof words);
    cpu->_st_cw = words[0] & 0xffff;
    cpu->_st_top = (words[1] >> 11) & 7;
    cpu->_st_sw_cond = words[1] & 0xffff & ~0x3800u;
    cpu->_st_tag = words[2] & 0xffff;
    cpu->_st_mmx_active = 0;
    for (unsigned n = 0; n < 8; n++) {
        unsigned i = (cpu->_st_top + n) & 7;
        memcpy(&cpu->_st[i], state + 28 + 10 * n, 8);
        memcpy(&cpu->_st_exp[i], state + 36 + 10 * n, 2);
    }
}
