#ifndef TRIBESPAD_GUEST_CURL_H
#define TRIBESPAD_GUEST_CURL_H
#include "guest_callback.h"
/* Library-lifetime string, valid before initialization and across cleanup.
   The caller must not free it. Repeated calls return the same guest address. */
uint32_t tribes_curl_version(_cpu *cpu);
uint32_t tribes_curl_global_init_mem(_cpu *cpu,uint32_t flags,uint32_t m,uint32_t f,uint32_t r,uint32_t s,uint32_t c);
uint32_t tribes_curl_multi_init(_cpu *cpu);
uint32_t tribes_curl_multi_cleanup(_cpu *cpu,uint32_t handle);
void tribes_curl_global_cleanup(_cpu *cpu);
uint32_t tribes_curl_easy_init(_cpu *cpu);
void tribes_curl_easy_cleanup(_cpu *cpu,uint32_t handle);
uint32_t tribes_curl_multi_add_handle(_cpu *cpu,uint32_t multi,uint32_t easy);
uint32_t tribes_curl_multi_remove_handle(_cpu *cpu,uint32_t multi,uint32_t easy);
/* Reviewed socket/timer options; other selectors stop explicitly. */
uint32_t tribes_curl_multi_setopt(_cpu *cpu,uint32_t handle,uint32_t option,uint32_t value);
uint32_t tribes_curl_multi_assign(_cpu *cpu,uint32_t multi,uint32_t socket,uint32_t value);
uint32_t tribes_curl_multi_socket_action(_cpu *cpu,uint32_t multi,uint32_t socket,uint32_t events,uint32_t output);
uint32_t tribes_curl_multi_info_read(_cpu *cpu,uint32_t multi,uint32_t output);
uint32_t tribes_curl_easy_setopt(_cpu *cpu,uint32_t easy,uint32_t option,uint32_t value);
uint32_t tribes_curl_easy_getinfo(_cpu *cpu,uint32_t easy,uint32_t info,uint32_t output);
uint32_t tribes_curl_easy_perform(_cpu *cpu,uint32_t easy);
uint32_t tribes_curl_slist_append(_cpu *,uint32_t,uint32_t);
void tribes_curl_slist_free_all(_cpu *,uint32_t);
uint32_t tribes_curl_url(_cpu *);
uint32_t tribes_curl_url_set(_cpu *,uint32_t,uint32_t,uint32_t,uint32_t);
void tribes_curl_url_cleanup(_cpu *,uint32_t);
#endif
