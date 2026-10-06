/* Offline raw x87 stack/BCD oracle, never shipped to devices. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 unsigned op,cw,ea; unsigned long long ma;
 while(scanf("%u %x %llx %x",&op,&cw,&ma,&ea)==4) {
  unsigned char a[10],z[10]={0},y[10]={0};uint64_t m=ma;uint16_t e=ea,c=cw,sw;
  memcpy(a,&m,8);memcpy(a+8,&e,2);
  __asm__ volatile("fninit; fldcw %0"::"m"(c):"st");
  if(op==0) __asm__ volatile("fbld %0"::"m"(a):"st");
  else __asm__ volatile("fldt %0"::"m"(a):"st");
  if(op==1) __asm__ volatile("fbstp %0":"=m"(z)::"st");
  else if(op==2) __asm__ volatile("fxtract":::"st");
  __asm__ volatile("fnstsw %0":"=m"(sw));
  if(op!=1) __asm__ volatile("fstpt %0":"=m"(z)::"st");
  if(op==2) __asm__ volatile("fstpt %0":"=m"(y)::"st");
  __asm__ volatile("fninit":::"st");
  for(unsigned i=0;i<10;i++)printf("%02x",z[i]);printf(" ");
  for(unsigned i=0;i<10;i++)printf("%02x",y[i]);printf(" %04x\n",sw);
 }
}
