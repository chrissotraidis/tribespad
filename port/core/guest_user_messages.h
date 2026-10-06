#ifndef TRIBESPAD_GUEST_USER_MESSAGES_H
#define TRIBESPAD_GUEST_USER_MESSAGES_H
#include "guest_callback.h"
uint32_t tribes_set_timer(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_kill_timer(uint64_t,uint32_t,uint32_t);
uint32_t tribes_peek_message(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_get_message(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_dispatch_message(_cpu *,uint32_t);
uint32_t tribes_translate_message(uint64_t,uint32_t);
uint32_t tribes_post_message(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t tribes_post_thread_message(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_post_quit_message(uint64_t,uint32_t);
void tribes_user_messages_remove_window(uint64_t,uint32_t);
#endif
