#ifndef TRIBESPAD_GUEST_GRAPHICS_H
#define TRIBESPAD_GUEST_GRAPHICS_H
#include <stdint.h>
int tribes_graphics_configure(uint64_t base);
void *tribes_graphics_native_lookup(uint32_t address);
#endif
