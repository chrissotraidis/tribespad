/* Offline x87 oracle for the eight remaining pure math operations. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void){
 unsigned op,cw,ae,be;unsigned long long am,bm;
 while(scanf("%u %x %llx %x %llx %x",&op,&cw,&am,&ae,&bm,&be)==6){
  unsigned char a[10],b[10],z[10];uint64_t x=am,y=bm,m;uint16_t e=ae,f=be,c=cw,sw;
  memcpy(a,&x,8);memcpy(a+8,&e,2);memcpy(b,&y,8);memcpy(b+8,&f,2);
  __asm__ volatile("fninit; fldcw %0"::"m"(c):"st");
  if(op>=4)__asm__ volatile("fldt %0"::"m"(b):"st");
  __asm__ volatile("fldt %0; fnclex"::"m"(a):"st");
  switch(op){
   case 0:__asm__ volatile("f2xm1":::"st");break;
   case 1:__asm__ volatile("fsin":::"st");break;
   case 2:__asm__ volatile("fcos":::"st");break;
   case 3:__asm__ volatile("fptan":::"st");break;
   case 4:__asm__ volatile("fyl2x":::"st");break;
   case 5:__asm__ volatile("fpatan":::"st");break;
   case 6:__asm__ volatile("1: fprem; fnstsw %%ax; testw $0x400, %%ax; jnz 1b":::"ax","cc","st");break;
   case 7:__asm__ volatile("fscale":::"st");break;
  }
  __asm__ volatile("fnstsw %0":"=m"(sw));
  if(op==3&&!(sw&0x400))__asm__ volatile("fstp %%st(0)":::"st");
  __asm__ volatile("fstpt %0; fninit":"=m"(z)::"st");
  memcpy(&m,z,8);memcpy(&e,z+8,2);
  printf("%016llx %04x %04x\n",(unsigned long long)m,e,sw);
 }
}
