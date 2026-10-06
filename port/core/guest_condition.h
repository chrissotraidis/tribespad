#ifndef TRIBESPAD_GUEST_CONDITION_H
#define TRIBESPAD_GUEST_CONDITION_H
#include <stdint.h>
void tribes_condition_initialize(uint64_t base,uint32_t condition);
void tribes_condition_wake(uint64_t base,uint32_t condition);
void tribes_condition_wake_all(uint64_t base,uint32_t condition);
uint32_t tribes_condition_sleep_srw(uint64_t base,uint32_t condition,uint32_t lock,uint32_t milliseconds,uint32_t flags);
uint32_t tribes_condition_sleep_cs(uint64_t base,uint32_t condition,uint32_t section,uint32_t milliseconds);
/* Synchronized diagnostic state, without reading or changing guest memory. */
uint32_t tribes_condition_waiters(uint64_t base,uint32_t condition);
uint32_t tribes_condition_live_count(void);
#endif
