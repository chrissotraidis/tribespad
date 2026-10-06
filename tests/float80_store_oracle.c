/* Offline x86-64/Rosetta reference; never linked into the Apple client. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
int main(void) {
 int translated=0;size_t size=sizeof(translated);
 if(sysctlbyname("sysctl.proc_translated",&translated,&size,0,0)||translated!=1)return 2;
 fprintf(stderr,"translated=1\n");
 unsigned width,pop,cw,seed,empty,exponent;unsigned long long significand;
 while(scanf("%u %u %x %x %u %llx %x",&width,&pop,&cw,&seed,&empty,&significand,&exponent)==7){
  unsigned char value[10],env[28];uint64_t bits=0,m=significand;uint16_t e=exponent,c=cw,sw;
  memcpy(value,&m,8);memcpy(value+8,&e,2);
  __asm__ volatile("fninit; fldt %1; fnstenv %0":"=m"(env):"m"(value):"st");
  memcpy(env,&c,2);sw=(7u<<11)|seed;memcpy(env+4,&sw,2);
  if(empty){uint16_t tags=0xffff;memcpy(env+8,&tags,2);}
  __asm__ volatile("fldenv %0"::"m"(env):"st");
  if(width==32){
   uint32_t z;
   if(pop)__asm__ volatile("fstps %0":"=m"(z)::"st");
   else __asm__ volatile("fsts %0":"=m"(z)::"st");
   bits=z;
  }else if(width==64){
   if(pop)__asm__ volatile("fstpl %0":"=m"(bits)::"st");
   else __asm__ volatile("fstl %0":"=m"(bits)::"st");
  }else return 3;
  __asm__ volatile("fnstsw %0; fninit":"=m"(sw)::"st");
  printf("%016llx %04x\n",(unsigned long long)bits,sw);
 }
}
