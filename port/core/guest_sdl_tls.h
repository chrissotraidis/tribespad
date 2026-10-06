#ifndef TRIBESPAD_GUEST_SDL_TLS_H
#define TRIBESPAD_GUEST_SDL_TLS_H
#include "guest_callback.h"
uint32_t tribes_sdl_get_tls(_cpu *cpu,uint32_t id_address);
uint32_t tribes_sdl_set_tls(_cpu *cpu,uint32_t id_address,uint32_t value,uint32_t destructor);
void tribes_sdl_cleanup_tls(_cpu *cpu);
/* Native SDL_Quit runs TLS destructors internally. These trusted scopes supply
   the live owner CPU; a native worker must never borrow a saved guest frame. */
void tribes_sdl_tls_cleanup_begin(_cpu *cpu);
void tribes_sdl_tls_cleanup_end(void);
#endif
