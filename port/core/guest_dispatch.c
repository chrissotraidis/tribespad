/* Finite original-PC dispatch into ahead-of-time ARM64 code. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../llasm-support/llasm_cpu.h"
_Static_assert(offsetof(_cpu, _esp) == 16, "LLVM guest stack ABI");
_Static_assert(offsetof(_cpu, _pointer_offset) == 112, "LLVM guest address ABI");
_Static_assert(offsetof(_cpu, _dispatch_target) == 172, "LLVM dispatch mailbox ABI");
/* A call through a function pointer the game never filled in. Name the
   caller (return address on the guest stack) so the missing provider can be
   found from the session log. */
static void report_null_call(_cpu *cpu){
    uint32_t words[8]={0};uint64_t offset=cpu->_pointer_offset;uint32_t sp=cpu->_esp;
    if(sp>=0x10000&&sp<0xffff0000u)for(unsigned i=0;i<8;i++)words[i]=*(const uint32_t *)(uintptr_t)(offset+sp+4*i);
    fprintf(stderr,"TribesPad: call to address 0 esp=0x%08x eax=0x%08x ecx=0x%08x edx=0x%08x stack=0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x,0x%08x\n",
        sp,cpu->_eax,cpu->_ecx,cpu->_edx,words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7]);
}
/* The report, out of line for an inlined copy of the setter (port/vengeance/vengeance_helpers_inline.c). */
void tribes_dispatch_null_call(_cpu *cpu){report_null_call(cpu);}
void tribes_set_dispatch_target(_cpu *cpu,uint32_t target) {
    if(__builtin_expect(!target,0))report_null_call(cpu);
    cpu->_dispatch_target=target;
}
extern const uint32_t tribes_dispatch_count;
extern const uint32_t tribes_dispatch_vas[];
extern void (*const tribes_dispatch_fns[])(_cpu *);
extern void tribes_return_to_host(_cpu *);
/* Strong native-provider implementation overrides this standalone-test default. */
__attribute__((weak,noinline)) void *tribes_native_lookup(uint32_t address) { (void)address;return NULL; }
__attribute__((weak,noinline)) void *tribes_dsound_native_lookup(uint32_t address) { (void)address;return NULL; }
__attribute__((weak,noinline)) void *tribes_graphics_native_lookup(uint32_t address) { (void)address;return NULL; }
/* Hosts with build-time patch variants must keep their original-address
   entries inaccessible until the matching guest bytes have been published. */
__attribute__((weak,noinline)) int tribes_dispatch_permitted(uint32_t address) { (void)address;return 1; }
#ifndef TRIBESPAD_DISABLE_DISPATCH_CACHE
/* Cache only immutable AOT table entries. Publication is checked before every
   hit; native providers may change availability and must still be consulted.
   One table for all threads: each slot is a single 64-bit word (the address,
   and the table index plus one above it), so a racing write leaves a valid
   entry, and no thread-local lookup is needed. 65536 slots (512 KB): Tribes:
   Vengeance's virtual calls reach far more targets than 256 held, and with 4096
   the collisions still sent 2.6% of its game thread through the table search. */
#include <stdatomic.h>
_Atomic uint64_t tribes_dispatch_cache[65536] = {0};   /* Tribes: Vengeance's dispatch stub reads it too (scripts/vengeance/build_engine.py) */
#endif
#ifdef TRIBESPAD_FIXED_DISPATCH
/* Tribes: Vengeance (scripts/vengeance/build_engine.py) has no publication
   gates, and its native routes (DirectInput and the system providers,
   native_exports.c) are fixed tables, so the cache holds those too: the game
   reaches Windows through this lookup on every call, FlsGetValue on nearly every
   C runtime call (uncached, a failed table search and a scan of the routes each:
   a fifth of the Host tab's 2.4 s). A slot keeps the function's offset from
   tribes_return_to_host above the address, so a hit in its dispatch stub is one
   load (with the table index, a second, dependent load from the 3.6 MB table). */
#define DISPATCH_BASE ((intptr_t)tribes_return_to_host)
static void (*cached_function(uint64_t entry))(_cpu *) {
    return (void (*)(_cpu *))(DISPATCH_BASE + (intptr_t)(int32_t)(uint32_t)(entry >> 32));
}
static void remember(unsigned slot, uint32_t address, void (*function)(_cpu *)) {
    intptr_t distance = (intptr_t)function - DISPATCH_BASE;
    if (distance && distance == (int32_t)distance)
        atomic_store_explicit(&tribes_dispatch_cache[slot], (uint64_t)address | (uint64_t)(uint32_t)(int32_t)distance << 32, memory_order_relaxed);
}
#endif
void (*tribes_lookup(uint32_t address))(_cpu *) {
#ifndef TRIBESPAD_FIXED_DISPATCH
    if(!tribes_dispatch_permitted(address)){
        fprintf(stderr,"TribesPad: unpublished original guest PC 0x%08x\n",address);abort();
    }
#endif
    if (address == UINT32_C(0xfffff000)) return tribes_return_to_host;
#ifndef TRIBESPAD_DISABLE_DISPATCH_CACHE
    unsigned slot = (address * UINT32_C(2654435761)) >> 16;
    uint64_t entry = atomic_load_explicit(&tribes_dispatch_cache[slot], memory_order_relaxed);
#ifdef TRIBESPAD_FIXED_DISPATCH
    if ((uint32_t)entry == address && (entry >> 32))
        return cached_function(entry);
#else
    if ((uint32_t)entry == address && (entry >> 32))
        return tribes_dispatch_fns[(entry >> 32) - 1];
#endif
#endif
    uint32_t lower = 0, upper = tribes_dispatch_count;
    while (lower < upper) {
        uint32_t mid = lower + (upper - lower) / 2;
        if (tribes_dispatch_vas[mid] < address) lower = mid + 1;
        else upper = mid;
    }
    if (lower < tribes_dispatch_count && tribes_dispatch_vas[lower] == address) {
#ifdef TRIBESPAD_FIXED_DISPATCH
        remember(slot, address, tribes_dispatch_fns[lower]);
#elif !defined(TRIBESPAD_DISABLE_DISPATCH_CACHE)
        atomic_store_explicit(&tribes_dispatch_cache[slot], (uint64_t)address | (uint64_t)(lower + 1) << 32, memory_order_relaxed);
#endif
        return tribes_dispatch_fns[lower];
    }
    void *native=tribes_native_lookup(address);
    if(native){
#ifdef TRIBESPAD_FIXED_DISPATCH
        remember(slot, address, (void (*)(_cpu *))native);
#endif
        return (void (*)(_cpu *))native;
    }
    native=tribes_dsound_native_lookup(address);if(native)return (void (*)(_cpu *))native;
    native=tribes_graphics_native_lookup(address);if(native)return (void (*)(_cpu *))native;
    fprintf(stderr, "TribesPad: unsupported original guest PC 0x%08x\n", address);
    abort();
}
