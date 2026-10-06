#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern void relocated_maddwd(_cpu *);
extern const unsigned char relocation_backing[97],loc_10143A00[32];
static uint32_t state=0x1a80e472;
static unsigned char random_byte(void){state^=state<<13;state^=state>>17;state^=state<<5;return (unsigned char)state;}
int main(void){
 unsigned char before[97];memcpy(before,relocation_backing,sizeof before);volatile uintptr_t label=(uintptr_t)loc_10143A00,backing=(uintptr_t)relocation_backing;assert(label==backing+17);
 for(unsigned i=0;i<65536;i++){
  _cpu cpu,expected;unsigned char input[32],output[32];for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=random_byte();
  cpu._pointer_offset=(uintptr_t)loc_10143A00-0x10143a00;cpu._mxcsr=i;expected=cpu;memcpy(input,cpu._xmm[0],16);memcpy(input+16,cpu._ymm_hi[0],16);
  for(unsigned j=0;j<32;j+=4){int16_t x0,x1,y0,y1;memcpy(&x0,input+j,2);memcpy(&x1,input+j+2,2);memcpy(&y0,loc_10143A00+j,2);memcpy(&y1,loc_10143A00+j+2,2);uint32_t value=(uint32_t)((int64_t)x0*y0+(int64_t)x1*y1);memcpy(output+j,&value,4);}
  memcpy(expected._xmm[0],output,16);memcpy(expected._ymm_hi[0],output+16,16);relocated_maddwd(&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(before,relocation_backing,sizeof before));
 }
 puts("PASS: 65536 original relocated MADDWD cases; original32-byte constant, guarded unaligned symbol, source alias, fullCPU/flags/allMXCSR");return 0;
}
