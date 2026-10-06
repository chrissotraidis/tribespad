/* Offline x86-64 Rosetta reference; not linked into the Apple client. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RUN(instruction) __asm__ volatile( \
 "movq %[a], %%rcx\n\tmovq %[b], %%rdx\n\tmovq %[c], %%rax\n\t" \
 "pushq %[flags]\n\tpopfq\n\t" instruction "\n\t" \
 "pushfq\n\tpopq %[after_flags]\n\t" \
 "movq %%rax, %[out0]\n\tmovq %%rcx, %[out1]\n\tmovq %%rdx, %[out2]\n\t" \
 : [after_flags] "=&r"(after_flags), [out0] "=m"(out[0]),[out1] "=m"(out[1]),[out2] "=m"(out[2]),[packed] "+m"(*packed) \
 : [a] "m"(a),[b] "m"(b),[c] "m"(c),[flags] "r"(flags) \
 : "rax","rcx","rdx","cc","memory")
int main(void){
 unsigned mode,initial_flags;unsigned long long aa,bb,cc;unsigned word;
 while(scanf("%u %x %llx %llx %llx %x",&mode,&initial_flags,&aa,&bb,&cc,&word)==6){
  if(mode>7)return 2;uint64_t a=aa,b=bb,c=cc,out[3],flags,after_flags;unsigned char backing[9];memset(backing,0xa5,9);uint32_t (*packed)=(void *)(backing+1);memcpy(packed,&word,4);
  __asm__ volatile("pushfq; popq %0":"=r"(flags));flags=(flags&~UINT64_C(0x8d5))|(initial_flags&0x8d5);
  switch(mode){
   case 0:RUN("shlx %%edx, %%ecx, %%eax");break;
   case 1:RUN("shlx %%edx, %[packed], %%eax");break;
   case 2:RUN("shlx %%edx, %%ecx, %%ecx");break;
   case 3:RUN("shlx %%edx, %%ecx, %%edx");break;
   case 4:RUN("shrx %%edx, %%ecx, %%eax");break;
   case 5:RUN("shrx %%edx, %[packed], %%eax");break;
   case 6:RUN("shrx %%edx, %%ecx, %%ecx");break;
   case 7:RUN("shrx %%edx, %%ecx, %%edx");break;
  }
  memcpy(&word,packed,4);if(backing[0]!=0xa5||backing[5]!=0xa5||backing[8]!=0xa5)return 4;
  printf("%016llx %016llx %016llx %x %03llx\n",(unsigned long long)out[0],(unsigned long long)out[1],(unsigned long long)out[2],word,(unsigned long long)(after_flags&0x8d5));
 }
 return ferror(stdin)?1:0;
}
