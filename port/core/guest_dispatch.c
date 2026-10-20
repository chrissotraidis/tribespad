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
   Private TLS avoids synchronization between concurrently executing guests. */
static _Thread_local struct {
    uint32_t address;
    void (*function)(_cpu *);
} dispatch_cache[256];
#endif
void (*tribes_lookup(uint32_t address))(_cpu *) {
    if(!tribes_dispatch_permitted(address)){
        fprintf(stderr,"TribesPad: unpublished original guest PC 0x%08x\n",address);abort();
    }
    if (address == UINT32_C(0xfffff000)) return tribes_return_to_host;
#ifndef TRIBESPAD_DISABLE_DISPATCH_CACHE
    unsigned slot = ((address >> 2) ^ (address >> 12)) & 255;
    if (dispatch_cache[slot].function && dispatch_cache[slot].address == address)
        return dispatch_cache[slot].function;
#endif
    uint32_t lower = 0, upper = tribes_dispatch_count;
    while (lower < upper) {
        uint32_t mid = lower + (upper - lower) / 2;
        if (tribes_dispatch_vas[mid] < address) lower = mid + 1;
        else upper = mid;
    }
    if (lower < tribes_dispatch_count && tribes_dispatch_vas[lower] == address) {
#ifndef TRIBESPAD_DISABLE_DISPATCH_CACHE
        dispatch_cache[slot].address = address;
        dispatch_cache[slot].function = tribes_dispatch_fns[lower];
#endif
        return tribes_dispatch_fns[lower];
    }
    void *native=tribes_native_lookup(address);if(native)return (void (*)(_cpu *))native;
    native=tribes_dsound_native_lookup(address);if(native)return (void (*)(_cpu *))native;
    native=tribes_graphics_native_lookup(address);if(native)return (void (*)(_cpu *))native;
    fprintf(stderr, "TribesPad: unsupported original guest PC 0x%08x\n", address);
    abort();
}
