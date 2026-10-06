/* Offline Rosetta/x86 reference, never linked into the client. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void){
 unsigned cw,seed,empty,exponent;unsigned long long significand;
 while(scanf("%x %x %u %llx %x",&cw,&seed,&empty,&significand,&exponent)==5){
  unsigned char value[10],env[28],result[10];uint64_t m=significand;uint16_t e=exponent,c=cw,sw;
  memcpy(value,&m,8);memcpy(value+8,&e,2);
  __asm__ volatile("fninit; fldt %1; fnstenv %0":"=m"(env):"m"(value):"st");
  memcpy(env,&c,2);sw=(7u<<11)|seed;memcpy(env+4,&sw,2);
  if(empty){uint16_t tags=0xffff;memcpy(env+8,&tags,2);}
  __asm__ volatile("fldenv %2; frndint; fnstsw %0; fstpt %1; fninit":"=m"(sw),"=m"(result):"m"(env):"st");
  memcpy(&m,result,8);memcpy(&e,result+8,2);printf("%016llx %04x %04x\n",(unsigned long long)m,e,sw);
 }
}
