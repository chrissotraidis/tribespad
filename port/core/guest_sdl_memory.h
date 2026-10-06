#ifndef TRIBESPAD_GUEST_SDL_MEMORY_H
#define TRIBESPAD_GUEST_SDL_MEMORY_H
#include <stdint.h>
/* SDL 3.2.18 guest allocator state; callback values are original x86 PCs.
   No host pointer conversion or native host-SDL allocator replacement. */
uint32_t tribes_sdl_set_memory_functions(uint64_t base,uint32_t malloc_pc,uint32_t calloc_pc,uint32_t realloc_pc,uint32_t free_pc);
int tribes_sdl_memory_functions(uint64_t base,uint32_t functions[4]);
const char *tribes_sdl_memory_error(void);
void tribes_sdl_memory_out_of_memory(void);
#endif
