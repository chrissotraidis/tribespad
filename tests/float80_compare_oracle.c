#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void){
 unsigned mode,ae,be;unsigned long long am,bm;
 while(scanf("%u %llx %x %llx %x",&mode,&am,&ae,&bm,&be)==5){
  unsigned char a[10],b[10];uint64_t x=am,y=bm;uint16_t e=ae,f=be,sw;
  memcpy(a,&x,8);memcpy(a+8,&e,2);memcpy(b,&y,8);memcpy(b+8,&f,2);
  __asm__ volatile("fninit; fldt %0; fldt %1; fnclex"::"m"(b),"m"(a):"st");
  switch(mode){
   case 0:__asm__ volatile("fcom %%st(1)":::"st");break;
   case 1:__asm__ volatile("fcomp %%st(1)":::"st");break;
   case 2:__asm__ volatile("fcompp":::"st");break;
   case 3:__asm__ volatile("fucom %%st(1)":::"st");break;
   case 4:__asm__ volatile("fucomp %%st(1)":::"st");break;
   default:__asm__ volatile("fucompp":::"st");break;
  }
  __asm__ volatile("fnstsw %0; fninit":"=m"(sw)::"st");
  printf("%04x\n",sw);
 }
}
