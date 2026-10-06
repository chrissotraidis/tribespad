#ifndef TRIBESPAD_GUEST_DATETIME_H
#define TRIBESPAD_GUEST_DATETIME_H
#include <stdint.h>
void tribes_get_local_time(uint64_t base,uint32_t output);
uint32_t tribes_get_date_format_ex(uint64_t base,uint32_t locale,uint32_t flags,uint32_t date,uint32_t format,uint32_t output,uint32_t capacity,uint32_t calendar);
uint32_t tribes_get_time_format_ex(uint64_t base,uint32_t locale,uint32_t flags,uint32_t date,uint32_t format,uint32_t output,uint32_t capacity);
#endif
