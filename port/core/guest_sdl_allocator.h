#ifndef TRIBESPAD_GUEST_SDL_ALLOCATOR_H
#define TRIBESPAD_GUEST_SDL_ALLOCATOR_H
#include "guest_callback.h"
/* Guest-facing memory consumers dispatch registered x86 cdecl callbacks.
   Call only after successful registration; default SDL allocator not provided. */
uint32_t tribes_sdl_malloc(_cpu *cpu,uint32_t size);
uint32_t tribes_sdl_calloc(_cpu *cpu,uint32_t count,uint32_t size);
uint32_t tribes_sdl_realloc(_cpu *cpu,uint32_t pointer,uint32_t size);
void tribes_sdl_free(_cpu *cpu,uint32_t pointer);
#endif
