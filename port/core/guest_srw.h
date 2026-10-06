#ifndef TRIBESPAD_GUEST_SRW_H
#define TRIBESPAD_GUEST_SRW_H
#include <stdint.h>
void tribes_srw_initialize(uint64_t base,uint32_t lock);
void tribes_srw_acquire_exclusive(uint64_t base,uint32_t lock);
void tribes_srw_release_exclusive(uint64_t base,uint32_t lock);
void tribes_srw_acquire_shared(uint64_t base,uint32_t lock);
void tribes_srw_release_shared(uint64_t base,uint32_t lock);
uint32_t tribes_srw_try_exclusive(uint64_t base,uint32_t lock);
uint32_t tribes_srw_try_shared(uint64_t base,uint32_t lock);
/* Locked diagnostic metadata; no ownership change or LastError mutation. */
uint32_t tribes_srw_live_count(void);
uint32_t tribes_srw_waiters(uint64_t base,uint32_t lock);
#endif
