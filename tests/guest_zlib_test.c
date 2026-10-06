#include "guest_zlib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cases.inc"

int main(int argc, char **argv) {
    if (argc == 2) {
        if (!strcmp(argv[1], "guest-overflow"))
            tribes_zlib_adler32(0, 1, UINT32_MAX, 2);
        else if (!strcmp(argv[1], "host-overflow"))
            tribes_zlib_adler32(UINT64_MAX, 1, 1, 1);
        else if (!strcmp(argv[1], "null"))
            tribes_zlib_adler32(0, 1, 0, 1);
        else if (!strcmp(argv[1], "overlap"))
            tribes_zlib_adler32_copy(0, 1, 1, 2, 2);
        return 1;
    }
    enum { SIZE = 0x30000 };
    unsigned char *arena = malloc(SIZE), *expected = malloc(SIZE);
    assert(arena && expected);
    unsigned total = 0;
    for (unsigned alignment = 0; alignment < 16; ++alignment) {
        for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
            const struct checksum_case *v = &cases[c];
            uint32_t src = 0x1010 + alignment, dst = 0x18020 + alignment;
            memset(arena, 0xa6, SIZE);
            for (uint32_t j = 0; j < v->length; ++j)
                arena[src + j] = (unsigned char)(j * 17 + v->length * 3);
            memcpy(expected, arena, SIZE);
            uint32_t result;
            if (v->copy) {
                memcpy(expected + dst, arena + src, v->length);
                result = tribes_zlib_adler32_copy((uintptr_t)arena, v->seed,
                                                   dst, src, v->length);
            } else {
                result = tribes_zlib_adler32((uintptr_t)arena, v->seed,
                                            src, v->length);
            }
            assert(result == v->result);
            assert(!memcmp(arena, expected, SIZE));
            ++total;
        }
    }
    free(expected); free(arena);
    printf("PASS: %u native guest-buffer checksum/copy cases\n", total);
    return 0;
}
