/* Finite original-PC dispatch into ahead-of-time ARM64 code. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../llasm-support/llasm_cpu.h"
_Static_assert(offsetof(_cpu, _esp) == 16, "LLVM guest stack ABI");
_Static_assert(offsetof(_cpu, _pointer_offset) == 112, "LLVM guest address ABI");
_Static_assert(offsetof(_cpu, _dispatch_target) == 172, "LLVM dispatch mailbox ABI");
void tribes_set_dispatch_target(_cpu *cpu,uint32_t target) { cpu->_dispatch_target=target; }
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
void (*tribes_lookup(uint32_t address))(_cpu *) {
    if(!tribes_dispatch_permitted(address)){
        fprintf(stderr,"TribesPad: unpublished original guest PC 0x%08x\n",address);abort();
    }
    if (address == UINT32_C(0xfffff000)) return tribes_return_to_host;
    uint32_t lower = 0, upper = tribes_dispatch_count;
    while (lower < upper) {
        uint32_t mid = lower + (upper - lower) / 2;
        if (tribes_dispatch_vas[mid] < address) lower = mid + 1;
        else upper = mid;
    }
    if (lower < tribes_dispatch_count && tribes_dispatch_vas[lower] == address)
        return tribes_dispatch_fns[lower];
    void *native=tribes_native_lookup(address);if(native)return (void (*)(_cpu *))native;
    native=tribes_dsound_native_lookup(address);if(native)return (void (*)(_cpu *))native;
    native=tribes_graphics_native_lookup(address);if(native)return (void (*)(_cpu *))native;
    fprintf(stderr, "TribesPad: unsupported original guest PC 0x%08x\n", address);
    abort();
}
