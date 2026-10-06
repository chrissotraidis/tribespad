#ifndef TRIBESPAD_GUEST_BCRYPT_H
#define TRIBESPAD_GUEST_BCRYPT_H
#include <stdint.h>
uint32_t tribes_bcrypt_gen_random(uint64_t base,uint32_t algorithm,uint32_t output,uint32_t length,uint32_t flags);
#endif
