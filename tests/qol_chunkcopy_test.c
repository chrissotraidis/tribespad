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
        _cpu cpu; memset(&cpu, 0, sizeof cpu);
        if (!strcmp(argv[1], "guest-overflow"))
            tribes_zlib_chunkcopy32_cpu(&cpu, UINT32_MAX, 1, 2);
        else if (!strcmp(argv[1], "host-overflow")) {
            cpu._pointer_offset = UINT64_MAX;
            tribes_zlib_chunkcopy32_cpu(&cpu, 1, 2, 1);
        } else if (!strcmp(argv[1], "null-source"))
            tribes_zlib_chunkcopy32_cpu(&cpu, 1, 0, 1);
        else if (!strcmp(argv[1], "null-destination"))
            tribes_zlib_chunkcopy32_cpu(&cpu, 0, 1, 1);
        return 1;
    }
    enum { SIZE = 0x6000 };
    unsigned char *arena = malloc(SIZE), *expected = malloc(SIZE);
    assert(arena && expected);
    unsigned total = 0;
    for (unsigned alignment = 0; alignment < 16; ++alignment) {
        uint32_t anchor = 0x1000 + alignment;
        for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
            const struct copy_case *v = &cases[c];
            uint32_t src = v->source_null ? 0 : anchor;
            uint32_t dst = v->destination_null ? 0 : anchor + v->distance;
            for (unsigned j = 0; j < SIZE; ++j)
                arena[j] = (unsigned char)((j - anchor) * 29 + 7);
            _cpu cpu; memset(&cpu, 0xa5, sizeof cpu);
            cpu._pointer_offset = (uintptr_t)arena;
            cpu._esp = 0x5010 + alignment - 12;
            cpu._mxcsr = 0xffffu - c;
            uint32_t args[3] = {dst, src, v->length};
            memcpy(arena + cpu._esp, args, sizeof args);
            memcpy(expected, arena, SIZE);
            uint32_t host_return = 0xfffff000;
            memcpy(expected + cpu._esp - 4, &host_return, 4);
            if (v->length) memcpy(expected + dst, payload + v->payload_offset, v->length);
            _cpu expected_cpu = cpu; expected_cpu._eax = dst + v->length;
            memset(expected_cpu._ymm_hi, 0, sizeof expected_cpu._ymm_hi);
            tribes_enter(&cpu, 0x100c7740);
            assert(!memcmp(&cpu, &expected_cpu, sizeof cpu));
            assert(!memcmp(arena, expected, SIZE));
            ++total;
        }
    }
    /* Short masked copies and full chunks must not touch the following page.
       The zero-length case points directly at two inaccessible pages. */
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    assert(page >= 1024);
    unsigned char *window = mmap(NULL, page * 6, PROT_NONE,
                                 MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(window != MAP_FAILED);
    assert(!mprotect(window + page, page, PROT_READ | PROT_WRITE));
    assert(!mprotect(window + page * 4, page, PROT_READ | PROT_WRITE));
    const unsigned extra[] = {127,128,129,255,256,257,1023,1024};
    unsigned char *source_snapshot = malloc(page);
    assert(source_snapshot);
    for (unsigned c = 0; c < 89; ++c) {
        unsigned n = c < 81 ? c : extra[c - 81];
        uint32_t src = (uint32_t)(page * 2 - n);
        uint32_t dst = (uint32_t)(page * 5 - n);
        memset(window + page, 0xa6, page);
        memset(window + page * 4, 0xb7, page);
        for (unsigned j = 0; j < n; ++j) window[src + j] = (unsigned char)(j * 17 + 3);
        memcpy(source_snapshot, window + page, page);
        _cpu cpu; memset(&cpu, 0xa5, sizeof cpu);
        cpu._pointer_offset = (uintptr_t)window;
        _cpu expected_cpu = cpu;
        memset(expected_cpu._ymm_hi, 0, sizeof expected_cpu._ymm_hi);
        assert(tribes_zlib_chunkcopy32_cpu(&cpu, dst, src, n) == dst + n);
        assert(!memcmp(&cpu, &expected_cpu, sizeof cpu));
        assert(!memcmp(window + page, source_snapshot, page));
        for (size_t j = 0; j < page - n; ++j) assert(window[page * 4 + j] == 0xb7);
        assert(!memcmp(window + dst, window + src, n));
        ++total;
    }
    free(source_snapshot);
    assert(!munmap(window, page * 6));
    free(expected); free(arena);
    printf("PASS: %u native fixed32 chunk-copy calls, overlap order, CPU and arena\n", total);
    return 0;
}
