#ifndef TRIBESPAD_GUEST_TLS_H
#define TRIBESPAD_GUEST_TLS_H
#include <stdint.h>
uint32_t tribes_tls_alloc(void);
uint32_t tribes_tls_free(uint32_t index);
uint32_t tribes_tls_get(uint32_t index);
uint32_t tribes_tls_set(uint32_t index,uint32_t value);
#endif
