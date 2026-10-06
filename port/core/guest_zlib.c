#include "guest_zlib.h"
#include "../llasm-support/llasm_cpu.h"
#include "zbuild.h"
#include "functable.h"
#include "zlib-ng.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reject arithmetic overflow before converting guest offsets. Mapping rights
   and fault delivery remain the responsibility of the guest address runtime.
   NULL checksum buffers return the original initialization value except for
   the one-byte path, which dereferences NULL in the original scalar routine. */
static void *buffer(uint64_t base, uint32_t address, uint32_t length) {
    uint32_t last = length ? length - 1 : 0;
    if (!address || address > UINT32_MAX - last ||
        base > UINTPTR_MAX - address - last) {
        fprintf(stderr, "TribesPad: invalid native zlib guest buffer range\n");
        abort();
    }
    return (void *)(uintptr_t)(base + address);
}

uint32_t tribes_zlib_adler32(uint64_t base, uint32_t seed,
                            uint32_t source, uint32_t length) {
    if (!source && length != 1) return 1;
    return zng_adler32(seed, buffer(base, source, length), length);
}

uint32_t tribes_zlib_adler32_copy(uint64_t base, uint32_t seed,
                                 uint32_t destination, uint32_t source,
                                 uint32_t length) {
    if (!length) return tribes_zlib_adler32(base, seed, source, 0);
    const uint8_t *src = buffer(base, source, length);
    uint8_t *dst = buffer(base, destination, length);
    /* The embedded routine has memcpy semantics: overlapping ranges are not
       a supported copy contract. Reject rather than silently use memmove. */
    if (length && (uint64_t)source < (uint64_t)destination + length &&
        (uint64_t)destination < (uint64_t)source + length) {
        fprintf(stderr, "TribesPad: overlapping native zlib copy buffers\n");
        abort();
    }
    return FUNCTABLE_CALL(adler32_fold_copy)(seed, dst, src, length);
}

uint32_t tribes_zlib_adler32_optimized(uint64_t base, uint32_t seed,
                                      uint32_t destination, uint32_t source,
                                      uint32_t length, uint32_t copy) {
    if (!source) return 1;
    if (!length) return seed;
    return copy ? tribes_zlib_adler32_copy(base, seed, destination, source, length)
                : tribes_zlib_adler32(base, seed, source, length);
}

uint32_t tribes_zlib_adler32_optimized_cpu(void *context, uint32_t seed,
                                          uint32_t destination, uint32_t source,
                                          uint32_t length, uint32_t copy) {
    _cpu *state = context;
    uint32_t result = tribes_zlib_adler32_optimized(state->_pointer_offset, seed,
                                                   destination, source, length, copy);
    /* Every original normal return executes VZEROUPPER. The current finite CPU
       stores IA-32 YMM upper halves; no ZMM bank is advertised or present. */
    memset(state->_ymm_hi, 0, sizeof state->_ymm_hi);
    return result;
}

/* Public three-argument VNNI checksum entry, with the same optimized
   NULL/zero-length contract and normal-return VZEROUPPER. */
uint32_t tribes_zlib_adler32_vnni_cpu(void *context, uint32_t seed,
                                    uint32_t source, uint32_t length) {
    return tribes_zlib_adler32_optimized_cpu(context, seed, 0, source, length, 0);
}

uint32_t tribes_zlib_adler32_copy_vnni_cpu(void *context, uint32_t seed,
                                         uint32_t destination, uint32_t source,
                                         uint32_t length) {
    return tribes_zlib_adler32_optimized_cpu(context, seed, destination,
                                            source, length, 1);
}

/* Original x86 fold-state finalization. These polynomial constants are pinned
   to IFC22's readonly table at 0x10149650..0x1014969f and the upstream x86
   crc32_pclmulqdq_tpl.h. Native architecture fold encodings are not substituted. */
static __uint128_t crc_polynomial_product(__uint128_t a, __uint128_t b,
                                         unsigned control) {
    uint64_t x = (uint64_t)(a >> ((control & 1) * 64));
    uint64_t y = (uint64_t)(b >> (((control >> 4) & 1) * 64));
    __uint128_t result = 0;
    for (unsigned bit = 0; bit < 64; ++bit)
        if ((y >> bit) & 1) result ^= (__uint128_t)x << bit;
    return result;
}
uint32_t tribes_zlib_crc_fold_final(uint8_t *state) {
    const __uint128_t k1 = ((__uint128_t)UINT64_C(0x1751997d0) << 64) | UINT64_C(0xccaa009e);
    const __uint128_t k5 = ((__uint128_t)UINT64_C(0x163cd6124) << 64) | UINT64_C(0xccaa009e);
    const __uint128_t k7 = ((__uint128_t)UINT64_C(0x1db710640) << 64) | UINT64_C(0x1f7011640);
    __uint128_t value; memcpy(&value, state, 16);
    for (unsigned lane = 1; lane < 4; ++lane) {
        __uint128_t next; memcpy(&next, state + lane * 16, 16);
        value = crc_polynomial_product(value, k1, 1) ^
                crc_polynomial_product(value, k1, 16) ^ next;
    }
    value = crc_polynomial_product(value, k5, 0) ^ (value >> 64);
    value = (crc_polynomial_product(value << 32, k5, 16) ^ value) &
             ~(__uint128_t)UINT32_MAX;
    __uint128_t intermediate = (crc_polynomial_product(value, k7, 0) ^ value) &
                               (__uint128_t)UINT64_MAX;
    __uint128_t final = crc_polynomial_product(intermediate, k7, 16) ^ intermediate ^ value;
    uint32_t result = ~(uint32_t)(final >> 64);
    memcpy(state + 64, &result, 4);
    return result;
}
uint32_t tribes_zlib_crc_fold_final_cpu(void *context, uint32_t address) {
    _cpu *state = context;
    uint32_t result = tribes_zlib_crc_fold_final(buffer(state->_pointer_offset, address, 68));
    /* The original routine has no VZEROUPPER: only written XMM0..4 clear
       their corresponding YMM upper halves. XMM5..7 are untouched. */
    memset(state->_ymm_hi, 0, sizeof state->_ymm_hi[0] * 5);
    return result;
}

uint32_t tribes_zlib_crc32_cpu(void *context, uint32_t seed,
                              uint32_t source, uint32_t length) {
    _cpu *state = context;
    /* Unlike zng_crc32(NULL), the original zero-length path preserves any
       seed without access. Positive NULL reads fault; buffer() stops them. */
    if (!length) return seed;
    uint32_t result = zng_crc32(seed, buffer(state->_pointer_offset, source, length), length);
    /* The original scalar path below16 bytes executes no VEX instructions.
       Larger inputs execute VZEROUPPER before their original fold call. */
    if (length >= 16) memset(state->_ymm_hi, 0, sizeof state->_ymm_hi);
    return result;
}


/* Private 256-byte bulk folding boundary from crc32_fold_vpclmulqdq_tpl.h.
   Four separate 16-byte state pointers and an XMM0 seed are intentional.
   The original caller admits only length >=256; shorter inputs are unsupported.
   Both tables are repeated 128-bit lanes in the pinned original image. */
uint32_t tribes_zlib_crc_fold_bulk(uint8_t *lanes[4], const uint8_t *source,
                                  uint32_t length, const uint8_t initial[16],
                                  uint32_t first) {
    if (length < 256) {
        fprintf(stderr, "TribesPad: unsupported short native CRC bulk fold\n");
        abort();
    }
    const __uint128_t k4 = ((__uint128_t)UINT64_C(0x154442bd4) << 64) | UINT64_C(0x1c6e41596);
    const __uint128_t k16 = ((__uint128_t)UINT64_C(0x11542778a) << 64) | UINT64_C(0x1322d1430);
    __uint128_t accumulator[4][4], previous[4], seed;
    for (unsigned lane = 0; lane < 4; ++lane) memcpy(&previous[lane], lanes[lane], 16);
    memcpy(accumulator, source, 256);
    if (first) { memcpy(&seed, initial, 16); accumulator[0][0] ^= seed; }
    for (unsigned lane = 0; lane < 4; ++lane)
        accumulator[0][lane] ^= crc_polynomial_product(previous[lane], k4, 1) ^
                                 crc_polynomial_product(previous[lane], k4, 16);
    uint32_t processed = length & ~UINT32_C(255);
    for (uint32_t offset = 256; offset < processed; offset += 256) {
        for (unsigned block = 0; block < 4; ++block) {
            for (unsigned lane = 0; lane < 4; ++lane) {
                __uint128_t next, value = accumulator[block][lane];
                memcpy(&next, source + offset + block * 64 + lane * 16, 16);
                accumulator[block][lane] = crc_polynomial_product(value, k16, 1) ^
                                           crc_polynomial_product(value, k16, 16) ^ next;
            }
        }
    }
    for (unsigned block = 1; block < 4; ++block) {
        for (unsigned lane = 0; lane < 4; ++lane) {
            __uint128_t value = accumulator[0][lane];
            accumulator[0][lane] = crc_polynomial_product(value, k4, 1) ^
                                   crc_polynomial_product(value, k4, 16) ^ accumulator[block][lane];
        }
    }
    for (unsigned lane = 0; lane < 4; ++lane) memcpy(lanes[lane], &accumulator[0][lane], 16);
    return processed;
}
uint32_t tribes_zlib_crc_fold_bulk_cpu(void *context, uint32_t lane0,
                                      uint32_t lane1, uint32_t lane2, uint32_t lane3,
                                      uint32_t source, uint32_t length, uint32_t first) {
    _cpu *state = context;
    uint32_t addresses[4] = {lane0, lane1, lane2, lane3};
    uint8_t *lanes[4];
    for (unsigned lane = 0; lane < 4; ++lane)
        lanes[lane] = buffer(state->_pointer_offset, addresses[lane], 16);
    const uint8_t *input = buffer(state->_pointer_offset, source, length & ~UINT32_C(255));
    uint32_t result = tribes_zlib_crc_fold_bulk(lanes, input, length, state->_xmm[0], first);
    memset(state->_ymm_hi, 0, sizeof state->_ymm_hi);
    return result;
}

/* Bulk copy loads each complete256-byte source block before any destination
   writes. Later overlapping reads observe earlier stores, matching the original.
   Fold state is written only after all processed source blocks are consumed. */
uint32_t tribes_zlib_crc_fold_bulk_copy(uint8_t *lanes[4], uint8_t *destination,
                                       const uint8_t *source, uint32_t length) {
    if (length < 256) {
        fprintf(stderr, "TribesPad: unsupported short native CRC bulk fold\n");
        abort();
    }
    const __uint128_t k4 = ((__uint128_t)UINT64_C(0x154442bd4) << 64) | UINT64_C(0x1c6e41596);
    const __uint128_t k16 = ((__uint128_t)UINT64_C(0x11542778a) << 64) | UINT64_C(0x1322d1430);
    __uint128_t accumulator[4][4], previous[4], chunk[4][4];
    for (unsigned lane = 0; lane < 4; ++lane) memcpy(&previous[lane], lanes[lane], 16);
    memcpy(chunk, source, 256);
    memcpy(accumulator, chunk, 256);
    for (unsigned lane = 0; lane < 4; ++lane)
        accumulator[0][lane] ^= crc_polynomial_product(previous[lane], k4, 1) ^
                                 crc_polynomial_product(previous[lane], k4, 16);
    memcpy(destination, chunk, 256);
    uint32_t processed = length & ~UINT32_C(255);
    for (uint32_t offset = 256; offset < processed; offset += 256) {
        memcpy(chunk, source + offset, 256);
        for (unsigned block = 0; block < 4; ++block) {
            for (unsigned lane = 0; lane < 4; ++lane) {
                __uint128_t next, value = accumulator[block][lane];
                memcpy(&next, &chunk[block][lane], 16);
                accumulator[block][lane] = crc_polynomial_product(value, k16, 1) ^
                                           crc_polynomial_product(value, k16, 16) ^ next;
            }
        }
        memcpy(destination + offset, chunk, 256);
    }
    for (unsigned block = 1; block < 4; ++block) {
        for (unsigned lane = 0; lane < 4; ++lane) {
            __uint128_t value = accumulator[0][lane];
            accumulator[0][lane] = crc_polynomial_product(value, k4, 1) ^
                                   crc_polynomial_product(value, k4, 16) ^ accumulator[block][lane];
        }
    }
    for (unsigned lane = 0; lane < 4; ++lane) memcpy(lanes[lane], &accumulator[0][lane], 16);
    return processed;
}
uint32_t tribes_zlib_crc_fold_bulk_copy_cpu(void *context, uint32_t lane0,
                                           uint32_t lane1, uint32_t lane2, uint32_t lane3,
                                           uint32_t destination, uint32_t source, uint32_t length) {
    _cpu *state = context;
    uint32_t addresses[4] = {lane0, lane1, lane2, lane3};
    uint8_t *lanes[4];
    for (unsigned lane = 0; lane < 4; ++lane)
        lanes[lane] = buffer(state->_pointer_offset, addresses[lane], 16);
    uint32_t processed = length & ~UINT32_C(255);
    const uint8_t *input = buffer(state->_pointer_offset, source, processed);
    uint8_t *output = buffer(state->_pointer_offset, destination, processed);
    uint32_t result = tribes_zlib_crc_fold_bulk_copy(lanes, output, input, length);
    memset(state->_ymm_hi, 0, sizeof state->_ymm_hi);
    return result;
}
