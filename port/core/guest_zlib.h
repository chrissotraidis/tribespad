#ifndef TRIBESPAD_GUEST_ZLIB_H
#define TRIBESPAD_GUEST_ZLIB_H
#include <stdint.h>
/* Buffer-only boundary to the pinned native zlib-ng. Guest stream structures,
   allocators and callbacks are not interchangeable with native structures. */
uint32_t tribes_zlib_adler32(uint64_t base, uint32_t seed,
                            uint32_t source, uint32_t length);
uint32_t tribes_zlib_adler32_copy(uint64_t base, uint32_t seed,
                                 uint32_t destination, uint32_t source,
                                 uint32_t length);
/* Five-argument internal optimized Adler contract, different NULL/zero-length
   behavior from the scalar leaves. Nonzero copy requests a buffer copy. */
uint32_t tribes_zlib_adler32_optimized(uint64_t base, uint32_t seed,
                                      uint32_t destination, uint32_t source,
                                      uint32_t length, uint32_t copy);
uint32_t tribes_zlib_adler32_optimized_cpu(void *context, uint32_t seed,
                                          uint32_t destination, uint32_t source,
                                          uint32_t length, uint32_t copy);
uint32_t tribes_zlib_adler32_vnni_cpu(void *context, uint32_t seed,
                                    uint32_t source, uint32_t length);
uint32_t tribes_zlib_adler32_copy_vnni_cpu(void *context, uint32_t seed,
                                         uint32_t destination, uint32_t source,
                                         uint32_t length);
uint32_t tribes_zlib_chunkcopy32_cpu(void *context, uint32_t destination,
                                    uint32_t source, uint32_t length);
uint32_t tribes_zlib_chunkmemset32_cpu(void *context, uint32_t destination,
                                      uint32_t source, uint32_t length);
uint32_t tribes_zlib_chunkmemset32_safe_cpu(void *context, uint32_t destination,
                                           uint32_t source, uint32_t length,
                                           uint32_t left);
uint32_t tribes_zlib_crc_fold_final(uint8_t *state);
uint32_t tribes_zlib_crc_fold_final_cpu(void *context, uint32_t address);
uint32_t tribes_zlib_crc32_cpu(void *context, uint32_t seed,
                              uint32_t source, uint32_t length);
uint32_t tribes_zlib_crc_fold_bulk(uint8_t *lanes[4], const uint8_t *source,
                                  uint32_t length, const uint8_t initial[16],
                                  uint32_t first);
uint32_t tribes_zlib_crc_fold_bulk_cpu(void *context, uint32_t lane0,
                                      uint32_t lane1, uint32_t lane2, uint32_t lane3,
                                      uint32_t source, uint32_t length, uint32_t first);
uint32_t tribes_zlib_crc_fold_bulk_copy(uint8_t *lanes[4], uint8_t *destination,
                                       const uint8_t *source, uint32_t length);
uint32_t tribes_zlib_crc_fold_bulk_copy_cpu(void *context, uint32_t lane0,
                                           uint32_t lane1, uint32_t lane2, uint32_t lane3,
                                           uint32_t destination, uint32_t source, uint32_t length);
#endif
