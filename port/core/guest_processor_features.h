#ifndef TRIBESPAD_GUEST_PROCESSOR_FEATURES_H
#define TRIBESPAD_GUEST_PROCESSOR_FEATURES_H
#include <stdint.h>
/* Finite Windows feature queries against the guest CPUID contract. */
uint32_t tribes_processor_feature_present(uint32_t feature);
/* SDL3 boolean queries use the same guest profile. */
uint32_t tribes_sdl_has_mmx(void);
uint32_t tribes_sdl_has_sse(void);
uint32_t tribes_sdl_has_avx(void);
#endif
