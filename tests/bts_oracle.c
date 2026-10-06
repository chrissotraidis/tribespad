#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define RUN(action) __asm__ volatile("movl %[input], %%eax\n\tmovl %[index], %%ecx\n\tpushq %[flags]\n\tpopfq\n\t" action "\n\tmovl %%eax, %[output]\n\tmovl %%ecx, %[after_index]\n\tpushfq\n\tpopq %[after_flags]" :[output]"=m"(output),[after_index]"=m"(after_index),[after_flags]"=&r"(after_flags):[input]"m"(input),[index]"m"(index),[flags]"r"(flags):"eax","ecx","cc","memory")
static uint32_t rnd(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
int main(void){unsigned long cases=0;uint32_t seed=0xb750;const uint32_t high[]={0,32,0x80000000u,0xffffffe0u,0x10000,0x12345660,0x7fffffe0,0xffffffc0};
for(unsigned mode=0;mode<3;++mode)for(unsigned i=0;i<65536;++i){uint32_t input=i<8192?(1u<<(i>>8)):i<8448?0:i<8704?UINT32_MAX:rnd(&seed),index=high[(i>>5)&7]|(i&31),output,after_index;uint64_t flags,after_flags;__asm__ volatile("pushfq; popq %0":"=r"(flags));flags=(flags&~UINT64_C(0x8d5))|(i&0x8d5);unsigned bit=mode==1?(input&31):mode==2?((i>>8)&31):(index&31);if(mode==0){RUN("btsl %%ecx, %%eax");}else if(mode==1){RUN("btsl %%eax, %%eax");}else{switch(i>>8){
#include "immediates.inc"
default:assert(0);
}}volatile uint32_t mask=1u<<bit;assert(output==(input|mask)&&after_index==index);assert((after_flags&~UINT64_C(0x894))==((flags&~UINT64_C(0x895))|((input>>bit)&1)));++cases;}printf("PASS: %lu canonical BTS REG32 distinct/self and IMM8 checks; defined CF/ZF and other defined flags agree\n",cases);return 0;}
