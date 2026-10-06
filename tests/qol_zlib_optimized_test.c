#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
            uint32_t args[5] = {v->seed, dst, src, v->length, v->copy};
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
            tribes_enter(&cpu, 0x100c7520);
            assert(!memcmp(&cpu, &expected_cpu, sizeof cpu));
            assert(!memcmp(arena, expected, SIZE));
            ++total;
        }
    }
    free(expected); free(arena);
    printf("PASS: %u native optimized Adler core cdecl calls, full CPU and arena\n", total);
    return 0;
}
