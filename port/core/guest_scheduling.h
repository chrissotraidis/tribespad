#ifndef TRIBESPAD_GUEST_SCHEDULING_H
#define TRIBESPAD_GUEST_SCHEDULING_H
#include <stdint.h>
uint32_t tribes_set_priority_class(uint32_t process,uint32_t priority);
uint32_t tribes_get_process_affinity_mask(uint64_t base,uint32_t process,uint32_t process_mask,uint32_t system_mask);
uint32_t tribes_set_thread_affinity_mask(uint32_t thread,uint32_t mask);
#endif
