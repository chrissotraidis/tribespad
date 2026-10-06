/* Offline x86 instruction oracle, executed through Rosetta on the build Mac.
   No emulation or translation runtime is included in the device application. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 unsigned mode,top;unsigned long long bits;
 while(scanf("%u %u %llx",&mode,&top,&bits)==3) {
  uint16_t before,after;double v;uint64_t raw=(uint64_t)bits;memcpy(&v,&raw,8);
  __asm__ volatile("fninit":::"st");
  if(mode==0) {
   for(unsigned n=0;n<((7-top)&7);n++)__asm__ volatile("fldz":::"st");
   __asm__ volatile("fldl %0"::"m"(v):"st");
  } else if(mode==1) {
   for(unsigned n=0;n<top;n++)__asm__ volatile("fincstp":::"st");
  } else {
   __asm__ volatile("movq %0, %%mm0"::"m"(raw):"mm0");
   if(mode==3)__asm__ volatile("emms":::"mm0");
  }
  __asm__ volatile("fnstsw %0; ftst; fnstsw %1; fninit":"=m"(before),"=m"(after)::"st");
  printf("%04x %04x\n",before,after);
 }
 return 0;
}
