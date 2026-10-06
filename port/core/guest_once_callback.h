#ifndef TRIBESPAD_GUEST_ONCE_CALLBACK_H
#define TRIBESPAD_GUEST_ONCE_CALLBACK_H
#include "guest_callback.h"
uint32_t tribes_init_once_execute(_cpu *cpu,uint32_t once,uint32_t callback,uint32_t parameter,uint32_t context);
#endif
