#ifndef TRIBESPAD_GUEST_COLLATION_H
#define TRIBESPAD_GUEST_COLLATION_H
#include <stdint.h>
uint32_t tribes_compare_string_ex(uint64_t base,uint32_t locale,uint32_t flags,uint32_t a,uint32_t na,uint32_t b,uint32_t nb,uint32_t version,uint32_t reserved,uint32_t handle);
#endif
