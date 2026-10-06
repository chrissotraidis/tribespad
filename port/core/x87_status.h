#ifndef TRIBESPAD_X87_STATUS_H
#define TRIBESPAD_X87_STATUS_H
#include <stdint.h>
/* Intel 80-bit extended format, independent of the host floating type. */
typedef struct { uint64_t significand; uint16_t sign_exponent; } TBExt80;
/* Returns status without TOP; unmasked exceptions set ES/B for later dispatch. */
uint16_t tb_x87_ftst80(TBExt80 value,uint16_t status,uint16_t control,int empty);
TBExt80 tb_x87_from_binary64(uint64_t bits);
#endif
