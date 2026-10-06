/* The finite Miles callbacks execute in the guest's flat data profile: DS/ES
   select the same base-zero, 4GiB read/write data descriptor (selector 0x23).
   PUSH Sreg uses the architecturally permitted zero-extended DWORD form,
   matching the original-x86 oracle. POP validates that this bounded code restores that same
   descriptor; arbitrary segment changes remain unsupported. */
#include "../llasm-support/llasm_cpu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void tribes_miles_push_flat_segment(_cpu *cpu){
 uint32_t selector=0x23;cpu->_esp-=4;memcpy((void *)(uintptr_t)(cpu->_pointer_offset+cpu->_esp),&selector,4);
}
void tribes_miles_pop_flat_segment(_cpu *cpu){
 uint16_t selector;memcpy(&selector,(const void *)(uintptr_t)(cpu->_pointer_offset+cpu->_esp),2);
 if(selector!=0x23){fputs("TribesPad: unsupported Miles flat segment selector\n",stderr);abort();}cpu->_esp+=4;
}
