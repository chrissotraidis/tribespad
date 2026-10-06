#ifndef TRIBESPAD_GUEST_VERSION_H
#define TRIBESPAD_GUEST_VERSION_H
#include <stdint.h>
uint32_t tribes_get_version(void);
uint32_t tribes_get_version_ex(uint64_t base,uint32_t output);
#endif
