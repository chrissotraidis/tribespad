#ifndef TRIBESPAD_GUEST_WINDOW_CATALOG_H
#define TRIBESPAD_GUEST_WINDOW_CATALOG_H
#include <stdint.h>
/* Owned top-level guest windows only. Registration precedes callbacks and
   removal follows destruction. SDL supplies current native style/focus. */
int tribes_window_add(uint64_t base,uint32_t hwnd,uint32_t instance,uint32_t style);
void tribes_window_remove(uint64_t base,uint32_t hwnd);
void tribes_window_update(uint64_t base,uint32_t hwnd,uint32_t style,int focused);
uint32_t tribes_window_known(uint64_t base,uint32_t hwnd);
uint32_t tribes_get_top_window(uint64_t base,uint32_t parent);
uint32_t tribes_get_window(uint64_t base,uint32_t hwnd,uint32_t command);
uint32_t tribes_get_window_thread_process_id(uint64_t base,uint32_t hwnd,uint32_t output);
uint32_t tribes_get_window_long_a(uint64_t base,uint32_t hwnd,uint32_t index);
uint32_t tribes_get_active_window(uint64_t base);
#endif
