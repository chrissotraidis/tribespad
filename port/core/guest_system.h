#ifndef TRIBESPAD_GUEST_SYSTEM_H
#define TRIBESPAD_GUEST_SYSTEM_H
#include <stdint.h>
/* Logical original-x86 address/commit profile. Apple backing pages can be larger;
   native subpage permission enforcement is a separate runtime requirement. */
#define TRIBES_GUEST_PAGE_SIZE 4096u
#define TRIBES_GUEST_ALLOCATION_GRANULARITY 65536u
#define TRIBES_GUEST_MINIMUM_ADDRESS 0x00010000u
#define TRIBES_GUEST_MAXIMUM_ADDRESS 0x7ffeffffu
void tribes_get_system_info(uint64_t base,uint32_t output);
void tribes_get_current_processor_number_ex(uint64_t base,uint32_t output);
uint32_t tribes_get_numa_processor_node_ex(uint64_t base,uint32_t processor,uint32_t output);
uint32_t tribes_get_numa_processor_node(uint64_t base,uint32_t processor,uint32_t output);
uint32_t tribes_get_numa_node_processor_mask_ex(uint64_t base,uint32_t node,uint32_t output);
uint32_t tribes_get_numa_highest_node_number(uint64_t base,uint32_t output);
#endif
