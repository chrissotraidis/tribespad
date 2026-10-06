#ifndef TRIBESPAD_GUEST_ADAPTERS_H
#define TRIBESPAD_GUEST_ADAPTERS_H
#include <stdint.h>
uint32_t tribes_get_adapters_addresses(uint64_t base,uint32_t family,uint32_t flags,uint32_t reserved,uint32_t output,uint32_t size_pointer);
#endif
