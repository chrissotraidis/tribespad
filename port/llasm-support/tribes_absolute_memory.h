#ifndef TRIBESPAD_ABSOLUTE_MEMORY_H
#define TRIBESPAD_ABSOLUTE_MEMORY_H
#include <stdint.h>
/* The inherited absolute-memory ABI has no CPU argument. The host must bind
 * its guest window on each native guest thread before entering translated code.
 * These helpers must be compiled with the memory pass for checked execution. */
int tribes_absolute_memory_bind(uint64_t base);
void tribes_absolute_memory_clear(void);
uint32_t x86_mov_reg_mem_8(uint32_t address);
uint32_t x86_mov_reg_mem_16(uint32_t address);
uint32_t x86_mov_reg_mem_32(uint32_t address);
void x86_mov_mem_reg_8(uint32_t address,uint32_t value);
void x86_mov_mem_reg_16(uint32_t address,uint32_t value);
void x86_mov_mem_reg_32(uint32_t address,uint32_t value);
#endif
