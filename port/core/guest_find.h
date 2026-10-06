#ifndef TRIBESPAD_GUEST_FIND_H
#define TRIBESPAD_GUEST_FIND_H
#include <stdint.h>
uint32_t tribes_find_first(uint64_t base,uint32_t pattern,uint32_t output);
uint32_t tribes_find_next(uint64_t base,uint32_t handle,uint32_t output);
uint32_t tribes_find_close(uint64_t base,uint32_t handle);
#endif
