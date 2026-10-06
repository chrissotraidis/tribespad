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
                    arena[src + j] = (unsigned char)(j * 17 + v->length * 3);
            }
            _cpu cpu; memset(&cpu, 0xa5, sizeof cpu);
            cpu._pointer_offset = (uintptr_t)arena;
            cpu._esp = 0x3a010 + alignment;
            cpu._mxcsr = 0xffffu - c;
            uint32_t args[4] = {v->seed, src, v->length, 0};
            unsigned count = 3;
            if (v->copy) {
                args[1] = dst; args[2] = src; args[3] = v->length;
                count = 4;
            }
            cpu._esp -= count * 4;
            memcpy(arena + cpu._esp, args, count * 4);
            memcpy(expected, arena, SIZE);
            uint32_t host_return = 0xfffff000;
            memcpy(expected + cpu._esp - 4, &host_return, 4);
            if (v->copy && v->length) {
                assert(src && dst);
                memcpy(expected + dst, arena + src, v->length);
            }
            _cpu expected_cpu = cpu;
            expected_cpu._eax = v->result;
            tribes_enter(&cpu, v->copy ? 0x100c1bf0 : 0x100c1c20);
            /* cdecl leaves arguments for the caller. All 440 CPU bytes except
               the return value stay intact, including vector/MXCSR state. */
            assert(!memcmp(&cpu, &expected_cpu, sizeof cpu));
            assert(!memcmp(arena, expected, SIZE));
            ++total;
        }
    }
    free(expected); free(arena);
    printf("PASS: %u original-PC cdecl checksum/copy calls, full CPU and arena\n", total);
    return 0;
}
