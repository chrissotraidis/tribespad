#ifndef TRIBESPAD_GUEST_OPENAL_H
#define TRIBESPAD_GUEST_OPENAL_H
#include <stdint.h>
#define TB_OPENAL_MODULE 0x20070000u
uint32_t tribes_openal_load(uint64_t base,const char *name);
void *tribes_openal_native_lookup(uint32_t address);
#endif
