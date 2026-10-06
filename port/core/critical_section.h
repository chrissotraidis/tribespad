#ifndef TRIBESPAD_CRITICAL_SECTION_H
#define TRIBESPAD_CRITICAL_SECTION_H
#include <stdint.h>
uint32_t tribes_critical_init(uint64_t guest_base,uint32_t guest_address);
uint32_t tribes_critical_init_spin(uint64_t guest_base,uint32_t guest_address,uint32_t spin);
uint32_t tribes_critical_init_ex(uint64_t guest_base,uint32_t guest_address,uint32_t spin,uint32_t flags);
uint32_t tribes_critical_enter(uint64_t guest_base,uint32_t guest_address);
uint32_t tribes_critical_try_enter(uint64_t guest_base,uint32_t guest_address);
uint32_t tribes_critical_leave(uint64_t guest_base,uint32_t guest_address);
uint32_t tribes_critical_delete(uint64_t guest_base,uint32_t guest_address);
/* Condition waits require depth one and retain a lifetime reference while
   unlocked. These two operations must be paired by the CV implementation. */
void tribes_critical_condition_release(uint64_t base,uint32_t guest);
void tribes_critical_condition_reacquire(uint64_t base,uint32_t guest);
#endif
