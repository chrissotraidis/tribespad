/* Finite lowering of the original CRT control query, NOT a general x87
   environment serializer. See fp_control_query_inputs.py's frame-use proof.
   FNSTENV/FLDENV are adjacent; only CW survives the pair in this private frame.
   Dead environment stores are eliminated, including untracked FIP/FDP/FOP.
   The original 28-byte read/write permission requirement is retained. */
#include "llasm_cpu.h"
#include "../core/guest_memory_checks.h"
#include <stdlib.h>
#include <string.h>

static void *frame(CPU, uint32_t displacement, uint32_t bytes) {
    uint32_t address = cpu->_ebp - displacement;
    if ((uint64_t)address + bytes > UINT64_C(0x100000000) ||
        cpu->_pointer_offset > UINTPTR_MAX - (uint64_t)address - bytes) abort();
    return (void *)(uintptr_t)(cpu->_pointer_offset + address);
}

void tb_fp_control_query_pair(CPU) {
    if (cpu->_st_top >= 8 || !cpu->_st_extended) abort();
    void *destination = frame(cpu, 32, 28);
    uint32_t token = tribes_memory_access_begin(destination, 28, 3);
    uint16_t saved = (uint16_t)cpu->_st_cw;
    /* The next original MOV reads four bytes, then ANDs with 0x1f3f.
       Only these two bytes are live; high reserved bits are dead too. */
    memcpy(destination, &saved, sizeof saved);
    cpu->_st_cw = (saved & 0x1f3f) | 0x40;
    /* FLDENV re-evaluates pending exceptions after restoring the masks. */
    cpu->_st_sw_cond &= ~UINT32_C(0x8080);
    if (cpu->_st_sw_cond & ~cpu->_st_cw & 0x3f)
        cpu->_st_sw_cond |= 0x8080;
    /* FLDENV classifies nonempty physical registers without changing data. */
    for (unsigned i = 0; i < 8; ++i) {
        if (((cpu->_st_tag >> (i * 2)) & 3) == 3) continue;
        uint64_t significand;
        memcpy(&significand, &cpu->_st[i], sizeof significand);
        unsigned exponent = cpu->_st_exp[i] & 0x7fff;
        unsigned tag = !exponent && !significand ? 1 :
            !exponent || exponent == 0x7fff || !(significand >> 63) ? 2 : 0;
        cpu->_st_tag = (cpu->_st_tag & ~(3u << (i * 2))) | (tag << (i * 2));
    }
    tribes_memory_access_end(token);
}

void tb_fp_control_query_mxcsr(CPU) {
    uint32_t value = cpu->_mxcsr & 0xffff;
    memcpy(frame(cpu, 4, 4), &value, sizeof value);
}
