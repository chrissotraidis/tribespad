#ifndef TRIBESPAD_MULTIMEDIA_TIMER_H
#define TRIBESPAD_MULTIMEDIA_TIMER_H
#include <stdint.h>
uint32_t tribes_time_caps(uint64_t base,uint32_t output,uint32_t bytes);
uint32_t tribes_time_begin_period(uint32_t period);
uint32_t tribes_time_end_period(uint32_t period);
#endif
