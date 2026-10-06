#ifndef TRIBESPAD_HIDDEN_WINDOWS_H
#define TRIBESPAD_HIDDEN_WINDOWS_H
#include "guest_callback.h"
uint32_t tribes_create_hidden_window(_cpu *cpu,uint32_t extended,uint32_t class_name,uint32_t title,uint32_t style,uint32_t x,uint32_t y,uint32_t width,uint32_t height,uint32_t parent,uint32_t menu,uint32_t instance,uint32_t parameter);
uint32_t tribes_default_window_proc(uint64_t base,uint32_t window,uint32_t message,uint32_t wp,uint32_t lp);
uint32_t tribes_is_window(uint64_t base,uint32_t window);
uint32_t tribes_destroy_hidden_window(_cpu *cpu,uint32_t window);
uint32_t tribes_dispatch_hidden_message(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
#endif
