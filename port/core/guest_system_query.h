#ifndef TRIBESPAD_GUEST_SYSTEM_QUERY_H
#define TRIBESPAD_GUEST_SYSTEM_QUERY_H
#include <stdint.h>
uint32_t tribes_nt_query_system_information(uint64_t base, uint32_t information_class,
    uint32_t output, uint32_t capacity, uint32_t return_length);
#endif
