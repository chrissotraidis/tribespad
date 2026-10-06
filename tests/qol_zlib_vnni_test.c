#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "cases.inc"
extern void tribes_enter(_cpu *, uint32_t);
int main(void) {
    enum { SIZE = 0x40000 };
    unsigned char *arena = malloc(SIZE), *expected = malloc(SIZE);
    assert(arena && expected);
    unsigned total = 0;
    for (unsigned alignment = 0; alignment < 16; ++alignment) {
        for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
            const struct checksum_case *v = &cases[c];
            uint32_t src = v->source_null ? 0 : 0x1010 + alignment;
            uint32_t dst = v->destination_null ? 0 : 0x18020 + alignment;
            memset(arena, 0xa6, SIZE);
            if (src) {
                assert(v->length <= 65536);
                for (uint32_t j = 0; j < v->length; ++j)
                    arena[src + j] = v->pattern ? 255 : (unsigned char)(j * 17 + v->length * 3);
            }
            _cpu cpu; memset(&cpu, 0xa5, sizeof cpu);
            cpu._pointer_offset = (uintptr_t)arena;
            cpu._esp = 0x3a010 + alignment - 20;
            cpu._mxcsr = 0xffffu - c;
            uint32_t args[3] = {v->seed, src, v->length};
            memcpy(arena + cpu._esp, args, sizeof args);
            memcpy(expected, arena, SIZE);
            uint32_t host_return = 0xfffff000;
            memcpy(expected + cpu._esp - 4, &host_return, 4);
            if (src && v->copy && v->length) {
                assert(dst);
                memcpy(expected + dst, arena + src, v->length);
            }
            _cpu expected_cpu = cpu; expected_cpu._eax = v->result;
            memset(expected_cpu._ymm_hi, 0, sizeof expected_cpu._ymm_hi);
            tribes_enter(&cpu, 0x100c8150);
            assert(!memcmp(&cpu, &expected_cpu, sizeof cpu));
            assert(!memcmp(arena, expected, SIZE));
            ++total;
        }
    }
    /* Place the checksum input exactly against an inaccessible page. This
       catches reads past length, including nonnull zero-length input. */
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char *guard = mmap(NULL, page * 2, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(guard != MAP_FAILED);
    assert(!mprotect(guard + page, page, PROT_NONE));
    unsigned protected_cases = 0;
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
        const struct checksum_case *v = &cases[c];
        /* Keep stack and input disjoint on both 4K and 16K page targets. */
        if (!v->source_null && v->length > 257) continue;
        assert(page > 1024);
        memset(guard, 0xa6, page);
        uint32_t src = v->source_null ? 0 : 0x1000 + (uint32_t)page - v->length;
        if (src)
            for (uint32_t j = 0; j < v->length; ++j)
                guard[page - v->length + j] = v->pattern ? 255 :
                    (unsigned char)(j * 17 + v->length * 3);
        _cpu cpu; memset(&cpu, 0xa5, sizeof cpu);
        cpu._pointer_offset = (uintptr_t)guard - 0x1000;
        cpu._esp = 0x1080;
        uint32_t args[3] = {v->seed, src, v->length};
        memcpy(guard + 0x80, args, sizeof args);
        assert(page <= SIZE);
        memcpy(expected, guard, page);
        uint32_t host_return = 0xfffff000;
        memcpy(expected + 0x7c, &host_return, 4);
        _cpu expected_cpu = cpu; expected_cpu._eax = v->result;
        memset(expected_cpu._ymm_hi, 0, sizeof expected_cpu._ymm_hi);
        tribes_enter(&cpu, 0x100c8150);
        assert(!memcmp(&cpu, &expected_cpu, sizeof cpu));
        assert(!memcmp(guard, expected, page));
        ++protected_cases; ++total;
    }
    assert(protected_cases == 1355);
    assert(!munmap(guard, page * 2));
    free(expected); free(arena);
    printf("PASS: %u native VNNI Adler entry cdecl calls, full CPU and arena\n", total);
    return 0;
}
