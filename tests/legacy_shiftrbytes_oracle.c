#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RUN(action) __asm__ volatile("ldmxcsr %[control]\n\tstmxcsr %[after_load]\n\tvmovdqu %[input], %%ymm1\n\tvmovdqu %[source], %%ymm2\n\tpushq %[flags]\n\tpopfq\n\t" action "\n\tvmovdqu %%ymm1, %[output]\n\tvmovdqu %%ymm2, %[after_source]\n\tpushfq\n\tpopq %[after_flags]\n\tstmxcsr %[after_control]\n\tldmxcsr %[saved]\n\tvzeroupper" :[output]"=m"(output),[after_source]"=m"(after_source),[after_flags]"=&r"(after_flags),[after_load]"=m"(after_load),[after_control]"=m"(after_control):[input]"m"(input),[source]"m"(source),[memory]"m"(memory),[control]"m"(control),[flags]"r"(flags),[saved]"m"(saved):"ymm1","ymm2","cc","memory")
int main(void){unsigned long cases=0;uint32_t saved;__asm__ volatile("stmxcsr %0":"=m"(saved));
for(unsigned count=0;count<256;++count)for(unsigned i=0;i<256;++i){unsigned index=count*256+i;unsigned char input[32],source[32],output[32],after_source[32];_Alignas(16) unsigned char memory[16];for(unsigned k=0;k<16;++k){input[k]=i<128?(k==i/8?(1u<<(i%8)):0):(unsigned char)(i*k+count+0x55);input[k+16]=(unsigned char)(index+k*7);source[k]=(unsigned char)~(index+k);source[k+16]=(unsigned char)(i+k*3);}memcpy(memory,source,16);uint32_t control=0x1f80|index,after_control,after_load;uint64_t flags,after_flags;__asm__ volatile("pushfq; popq %0":"=r"(flags));flags=(flags&~UINT64_C(0x8d5))|(index&0x8d5);switch(count){
#include "shifts.inc"
default:assert(0);
}for(unsigned k=0;k<16;++k)assert(output[k]==(k+count<16?input[k+count]:0));assert(!memcmp(output+16,input+16,16)&&!memcmp(after_source,source,32));assert(after_load==control&&after_control==control&&(after_flags&0x8d5)==(flags&0x8d5));++cases;}printf("PASS: %lu canonical legacy PSRLDQ checks; all256 counts, preserved YMM/flags/MXCSR\n",cases);return 0;}
