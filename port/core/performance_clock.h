#ifndef TRIBESPAD_PERFORMANCE_CLOCK_H
#define TRIBESPAD_PERFORMANCE_CLOCK_H
#include <stdint.h>
uint32_t tribes_performance_frequency(uint64_t base,uint32_t output);
uint32_t tribes_performance_counter(uint64_t base,uint32_t output);
uint32_t tribes_tick_count(void);
void tribes_sleep(uint32_t milliseconds);
void tribes_system_file_time(uint64_t base,uint32_t output);
#endif
