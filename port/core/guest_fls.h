#ifndef TRIBESPAD_GUEST_FLS_H
#define TRIBESPAD_GUEST_FLS_H
#include "guest_callback.h"
uint32_t tribes_fls_alloc(_cpu *cpu,uint32_t callback);
uint32_t tribes_fls_get(_cpu *cpu,uint32_t index);
uint32_t tribes_fls_set(_cpu *cpu,uint32_t index,uint32_t value);
uint32_t tribes_fls_free(_cpu *cpu,uint32_t index);
#endif
