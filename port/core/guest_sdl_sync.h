#ifndef TRIBESPAD_GUEST_SDL_SYNC_H
#define TRIBESPAD_GUEST_SDL_SYNC_H
#include <stdint.h>
uint32_t tribes_sdl_create_mutex(uint64_t base);
void tribes_sdl_destroy_mutex(uint64_t base,uint32_t token);
void tribes_sdl_lock_mutex(uint64_t base,uint32_t token);
uint32_t tribes_sdl_try_lock_mutex(uint64_t base,uint32_t token);
void tribes_sdl_unlock_mutex(uint64_t base,uint32_t token);
uint32_t tribes_sdl_create_condition(uint64_t base);
void tribes_sdl_destroy_condition(uint64_t base,uint32_t token);
void tribes_sdl_signal_condition(uint64_t base,uint32_t token);
void tribes_sdl_broadcast_condition(uint64_t base,uint32_t token);
uint32_t tribes_sdl_wait_condition_timeout(uint64_t base,uint32_t condition,uint32_t mutex,int32_t milliseconds);
#endif
