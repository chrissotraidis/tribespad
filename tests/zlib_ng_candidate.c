/* Exercise the pinned native library, including its dispatched copy routine. */
#include "zbuild.h"
#include "functable.h"
#include "zlib-ng.h"

uint32_t tb_test_adler(uint32_t seed, const uint8_t *src, uint32_t len) {
    return zng_adler32(seed, src, len);
}

uint32_t tb_test_adler_copy(uint32_t seed, uint8_t *dst,
                            const uint8_t *src, uint32_t len) {
    return FUNCTABLE_CALL(adler32_fold_copy)(seed, dst, src, len);
}
