#ifndef TRIBESPAD_GUEST_ONCE_H
#define TRIBESPAD_GUEST_ONCE_H
#include <stdint.h>
/* Host callback dispatch protocol. Begin returns 1 for initialization ownership,
   2 for completed context, 0 for bad arguments. Complete is owner-thread only.
   Guest INIT_ONCE is a DWORD; native mutex/condition state is outside guest RAM. */
uint32_t tribes_once_begin(uint64_t base,uint32_t once,uint32_t *context);
uint32_t tribes_once_complete(uint64_t base,uint32_t once,uint32_t success,uint32_t context);
void tribes_once_initialize(uint64_t base,uint32_t once);
#endif
