#include "tribes_absolute_memory.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static _Thread_local uint64_t guest_base;
int tribes_absolute_memory_bind(uint64_t base) {
 if(!base || base>UINTPTR_MAX-UINT64_C(0xffffffff))return 0;
 guest_base=base;return 1;
}
void tribes_absolute_memory_clear(void) {guest_base=0;}
static void *address_of(uint32_t address,unsigned bytes) {
 if(!guest_base || (uint64_t)address+bytes>UINT64_C(0x100000000)) {
  fputs("TribesPad: invalid absolute guest memory access\n",stderr);abort();
 }
 return (void *)(uintptr_t)(guest_base+address);
}
/* memcpy permits unaligned guest operands and gives the compiler memory pass
 * one complete range to validate before a write, including page crossings.
 * The Apple ARM64 targets are little endian, as is the original x86 image. */
uint32_t x86_mov_reg_mem_8(uint32_t address) {uint8_t value;memcpy(&value,address_of(address,1),1);return value;}
uint32_t x86_mov_reg_mem_16(uint32_t address) {uint16_t value;memcpy(&value,address_of(address,2),2);return value;}
uint32_t x86_mov_reg_mem_32(uint32_t address) {uint32_t value;memcpy(&value,address_of(address,4),4);return value;}
void x86_mov_mem_reg_8(uint32_t address,uint32_t value) {uint8_t narrowed=(uint8_t)value;memcpy(address_of(address,1),&narrowed,1);}
void x86_mov_mem_reg_16(uint32_t address,uint32_t value) {uint16_t narrowed=(uint16_t)value;memcpy(address_of(address,2),&narrowed,2);}
void x86_mov_mem_reg_32(uint32_t address,uint32_t value) {memcpy(address_of(address,4),&value,4);}
