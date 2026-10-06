#include "../port/core/shift.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t random_word(uint32_t *s){*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s;}
int main(void){unsigned long cases=0;uint32_t seed=0x25034;
for(unsigned count=0;count<256;++count)for(unsigned i=0;i<64;++i)for(unsigned f=0;f<4;++f){uint32_t left=i<32?1u<<i:random_word(&seed),source=i<32?~left:random_word(&seed);uint32_t initial=f==0?0:f==1?0x8d5:f==2?0x41:0x884;uint64_t flags,after;__asm__ volatile("pushfq; popq %0":"=r"(flags));flags=(flags&~UINT64_C(0x8d5))|initial;TBShiftResult expected=tb_shift(TB_SHLD,32,left,source,count,(uint32_t)flags);uint32_t actual=left;
__asm__ volatile("pushq %[flags]; popfq; shld %%cl, %%edx, %%eax; pushfq; popq %[after]" : "+a"(actual),[after]"=&r"(after) : "d"(source),"c"(count),[flags]"r"(flags) : "cc","memory");
unsigned n=count&31;uint32_t defined=0x8d5;if(n){defined&=~16u;if(n!=1)defined&=~2048u;}assert(actual==expected.value);assert(((uint32_t)after&defined)==(expected.flags&defined));if(n){assert((expected.flags&16)==(initial&16));if(n!=1)assert((expected.flags&2048)==(initial&2048));}++cases;}
printf("PASS: %lu canonical legacy SHLD REG32/CL result/defined-flag checks; undefined flags excluded\n",cases);return 0;}
