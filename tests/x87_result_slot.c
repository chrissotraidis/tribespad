/* Test-only, reserved guest result slot for inherited wide-result helpers. */
#include "../port/llasm-support/llasm_cpu.h"
#include <string.h>
uint32_t halopad_x87_result(CPU,const int64_t *value) {
 memcpy(REG2PTR(0x500000),value,8);return 0x500000;
}
