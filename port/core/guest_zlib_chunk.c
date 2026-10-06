/* Fixed 32-byte chunk-copy contract of the pinned zlib-ng AVX512 helper.
   Algorithm reference: zlib-ng 2.2.3 arch/x86/chunkset_avx512.c, zlib license
   retained in ref/zlib-ng/LICENSE.md. Each load completes before its store;
   overlapping buffers deliberately retain the original chunk ordering. */
#include "guest_zlib.h"
#include "../llasm-support/llasm_cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *range(uint64_t base, uint32_t address, uint32_t length) {
    if (!address || address > UINT32_MAX - (length - 1) ||
        base > UINTPTR_MAX - address - (length - 1)) {
        fprintf(stderr, "TribesPad: invalid native chunk-copy guest range\n");
        abort();
    }
    return (uint8_t *)(uintptr_t)(base + address);
}

uint32_t tribes_zlib_chunkcopy32_cpu(void *context, uint32_t destination,
                                    uint32_t source, uint32_t length) {
    _cpu *state = context;
    uint32_t result = destination + length;
    if (length) {
        uint8_t *out = range(state->_pointer_offset, destination, length);
        const uint8_t *from = range(state->_pointer_offset, source, length);
        uint8_t chunk[32];
        if (length < 32) {
            memcpy(chunk, from, length);
            memcpy(out, chunk, length);
        } else {
            unsigned remainder = length & 31;
            if (!remainder) remainder = 32;
            memcpy(chunk, from, 32);
            memcpy(out, chunk, 32);
            out += remainder; from += remainder; length -= remainder;
            while (length) {
                memcpy(chunk, from, 32);
                memcpy(out, chunk, 32);
                out += 32; from += 32; length -= 32;
            }
        }
    }
    memset(state->_ymm_hi, 0, sizeof state->_ymm_hi);
    return result;
}

uint32_t tribes_zlib_chunkmemset32_cpu(void *context, uint32_t destination,
                                      uint32_t source, uint32_t length) {
    _cpu *state = context;
    int32_t difference;
    uint32_t bits = destination - source;
    memcpy(&difference, &bits, sizeof difference);
    uint32_t distance = (uint32_t)(difference < 0 ? -(int64_t)difference : difference);
    /* This private helper's upstream precondition excludes zero distance.
       Zero length also has branch-dependent reads/returns in the release
       binary; its safe public wrapper never calls it with zero length. */
    if (!length || !distance) {
        fprintf(stderr, "TribesPad: unsupported private chunkmemset precondition\n");
        abort();
    }
    if (difference < 0 && distance < length) {
        if (length <= 32) return tribes_zlib_chunkcopy32_cpu(context, destination, source, length);
        memmove(range(state->_pointer_offset, destination, length),
                range(state->_pointer_offset, source, length), length);
    } else if (distance == 1) {
        uint8_t value = *range(state->_pointer_offset, source, 1);
        memset(range(state->_pointer_offset, destination, length), value, length);
    } else if (distance >= 32) {
        return tribes_zlib_chunkcopy32_cpu(context, destination, source, length);
    } else if (length <= 16 && distance >= 16) {
        uint8_t snapshot[16];
        memcpy(snapshot, range(state->_pointer_offset, source, length), length);
        memcpy(range(state->_pointer_offset, destination, length), snapshot, length);
    } else {
        /* The original permutation table yields a repeating distance-byte
           pattern. These advance amounts retain its exact store ordering,
           including its special 10/11/12-byte choices. */
        static const uint8_t remainder[32] = {
            0,0,0,2,0,2,2,4,0,5,22,21,20,6,4,2,
            0,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1
        };
        uint8_t pattern[32];
        const uint8_t *from = range(state->_pointer_offset, source, distance);
        for (unsigned j = 0; j < 32; ++j) pattern[j] = from[j % distance];
        uint8_t *out = range(state->_pointer_offset, destination, length);
        uint32_t remaining = length;
        unsigned advance = 32 - remainder[distance];
        while (remaining >= 32) {
            memcpy(out, pattern, 32);
            out += advance; remaining -= advance;
        }
        if (remaining) memcpy(out, pattern, remaining);
    }
    memset(state->_ymm_hi, 0, sizeof state->_ymm_hi);
    return destination + length;
}

uint32_t tribes_zlib_chunkmemset32_safe_cpu(void *context, uint32_t destination,
                                           uint32_t source, uint32_t length,
                                           uint32_t left) {
    _cpu *state = context;
    uint32_t count = length < left ? length : left;
    if (!count) return destination;
    uint8_t *out = range(state->_pointer_offset, destination, count);
    unsigned peel = (unsigned)(-destination) & 3;
    if (peel > count) peel = count;
    const uint8_t *from = peel ? range(state->_pointer_offset, source, peel) : NULL;
    while ((destination & 3) && count) {
        *out++ = *from++;
        ++destination; ++source; --count;
    }
    /* No VZEROUPPER occurs if the scalar peel consumes the entire copy. */
    return count ? tribes_zlib_chunkmemset32_cpu(context, destination, source, count)
                 : destination;
}
