#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern void relocated_xor(_cpu *);
extern const unsigned char relocation_backing[64],loc_101CB1A0[16];
static uint32_t state=0x7bc32541;
static unsigned char next_byte(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void){
 unsigned char before[64];memcpy(before,relocation_backing,64);
 volatile uintptr_t label_address=(uintptr_t)loc_101CB1A0, backing_address=(uintptr_t)relocation_backing;
 assert(label_address==backing_address+17);
 for(unsigned control=0;control<65536;control++){
  _cpu cpu,expected;unsigned char *bytes=(void *)&cpu;
  for(unsigned j=0;j<sizeof cpu;j++)bytes[j]=next_byte();
  cpu._pointer_offset=(uintptr_t)loc_101CB1A0-0x101cb1a0;cpu._mxcsr=control;expected=cpu;
  for(unsigned j=0;j<16;j++)expected._xmm[0][j]^=loc_101CB1A0[j];
  memset(expected._ymm_hi[0],0,16);relocated_xor(&cpu);
  assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(before,relocation_backing,64));
 }
 puts("PASS: 65536 relocated original-byte AOT VXORPS cases; full CPU/MXCSR, alias snapshot, upper-half clearing and 64-byte data/guards preserved");return 0;
}
