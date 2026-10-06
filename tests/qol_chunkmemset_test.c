#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_zlib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "cases.inc"
extern void tribes_enter(_cpu *, uint32_t);
int main(int argc, char **argv) {
    if (argc == 2) {
        unsigned char scratch[32] = {7}; _cpu cpu; memset(&cpu, 0, sizeof cpu);
        cpu._pointer_offset = (uintptr_t)scratch;
        if (!strcmp(argv[1], "guest-overflow")) tribes_zlib_chunkmemset32_cpu(&cpu, UINT32_MAX, 1, 2);
        else if (!strcmp(argv[1], "host-overflow")) {
            cpu._pointer_offset = UINT64_MAX; tribes_zlib_chunkmemset32_cpu(&cpu, 1, 2, 1);
        } else if (!strcmp(argv[1], "null-source")) tribes_zlib_chunkmemset32_cpu(&cpu, 1, 0, 1);
        else if (!strcmp(argv[1], "null-destination")) tribes_zlib_chunkmemset32_cpu(&cpu, 0, 1, 1);
        else if (!strcmp(argv[1], "zero-private-length")) tribes_zlib_chunkmemset32_cpu(&cpu, 2, 1, 0);
        else if (!strcmp(argv[1], "zero-distance")) tribes_zlib_chunkmemset32_cpu(&cpu, 1, 1, 1);
        return 1;
    }
    enum { SIZE = 0x6000 };
    unsigned char *arena = malloc(SIZE), *expected = malloc(SIZE); assert(arena && expected);
    unsigned total = 0;
    for (unsigned alignment = 0; alignment < 16; ++alignment) {
        uint32_t anchor = 0x1000 + alignment;
        for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
            const struct pattern_case *v = &cases[c];
            uint32_t src = anchor, dst = anchor + v->distance;
            for (unsigned j = 0; j < SIZE; ++j) arena[j] = (unsigned char)((j - anchor) * 29 + 7);
            _cpu cpu; memset(&cpu, 0xa5, sizeof cpu); cpu._pointer_offset = (uintptr_t)arena;
            unsigned count = v->safe ? 4 : 3;
            cpu._esp = 0x5010 + alignment - count * 4; cpu._mxcsr = 0xffffu - c;
            uint32_t args[4] = {dst,src,v->length,v->left};
            memcpy(arena + cpu._esp, args, count * 4); memcpy(expected, arena, SIZE);
            uint32_t host_return = 0xfffff000; memcpy(expected + cpu._esp - 4, &host_return, 4);
            if (v->count) memcpy(expected + dst, payload + v->payload_offset, v->count);
            _cpu expected_cpu = cpu; expected_cpu._eax = dst + v->count;
            if (!v->safe || v->count > ((-dst)&3)) memset(expected_cpu._ymm_hi, 0, sizeof expected_cpu._ymm_hi);
            tribes_enter(&cpu, v->safe ? 0x100c7a80 : 0x100c77c0);
            assert(!memcmp(&cpu, &expected_cpu, sizeof cpu)); assert(!memcmp(arena, expected, SIZE)); ++total;
        }
    }
    size_t page = (size_t)sysconf(_SC_PAGESIZE); assert(page >= 1024);
    unsigned char *window = mmap(NULL, page*3, PROT_NONE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert(window != MAP_FAILED); assert(!mprotect(window + page, page, PROT_READ|PROT_WRITE));
    const unsigned distances[] = {1,2,3,4,5,6,10,11,12,15,16,17,31,32,33,65};
    const unsigned extra[] = {127,128,129,255,256,257};
    unsigned char *snapshot = malloc(page), *guard_expected = malloc(page); assert(snapshot && guard_expected);
    for (unsigned safe = 0; safe < 2; ++safe) for (unsigned c = 0; c < 71; ++c) {
        unsigned n = c < 65 ? c+1 : extra[c-65];
        for (unsigned d = 0; d < sizeof distances/sizeof distances[0]; ++d) {
            uint32_t dst = (uint32_t)(page*2-n), src = dst-distances[d];
            for (size_t j = 0; j < page; ++j) window[page+j] = (unsigned char)(j*29+7);
            memcpy(snapshot, window+page, page); memcpy(guard_expected, snapshot, page);
            for (unsigned j = 0; j < n; ++j) guard_expected[dst-page+j] = snapshot[src-page+j%distances[d]];
            _cpu cpu; memset(&cpu, 0xa5, sizeof cpu); cpu._pointer_offset = (uintptr_t)window;
            _cpu expected_cpu = cpu;
            if (!safe || n > ((-dst)&3)) memset(expected_cpu._ymm_hi,0,sizeof expected_cpu._ymm_hi);
            uint32_t result = safe ? tribes_zlib_chunkmemset32_safe_cpu(&cpu,dst,src,n,n)
                                   : tribes_zlib_chunkmemset32_cpu(&cpu,dst,src,n);
            assert(result == dst+n); assert(!memcmp(&cpu,&expected_cpu,sizeof cpu));
            assert(!memcmp(window+page,guard_expected,page)); ++total;
        }
    }
    free(guard_expected); free(snapshot); assert(!munmap(window,page*3)); free(expected); free(arena);
    printf("PASS: %u native chunkmemset/safe calls, patterns, clamping, CPU and protected pages\n",total);
    return 0;
}
