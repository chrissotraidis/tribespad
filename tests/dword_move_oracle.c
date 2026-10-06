#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RUN(action) __asm__ volatile( \
 "ldmxcsr %[control]\n\tvmovdqu %[input], %%ymm1\n\tvmovdqu %[merge], %%ymm2\n\tmovl %[scalar], %%ecx\n\tmovl $0xdeadbeef, %%eax\n\tpushq %[flags]\n\tpopfq\n\t" action "\n\tmovl %%eax, %[result]\n\tpushfq\n\tpopq %[after_flags]\n\tstmxcsr %[after_control]\n\tvmovdqu %%ymm1, %[output]\n\tvmovdqu %%ymm2, %[after_merge]\n\tldmxcsr %[saved]\n\tvzeroupper" \
 : [result] "=&r"(result),[after_flags] "=&r"(after_flags),[after_control] "=m"(after_control),[output] "=m"(output),[after_merge] "=m"(after_merge),[memory] "+m"(*memory) \
 : [control] "m"(control),[input] "m"(input),[merge] "m"(merge),[scalar] "r"(scalar),[flags] "r"(flags),[saved] "m"(saved) \
 : "eax","ecx","ymm1","ymm2","cc","memory")
int main(void) {
 unsigned mode,imm,control,initial_flags,scalar,input[8],merge[8];
 while(scanf("%u %u %x %x %x",&mode,&imm,&control,&initial_flags,&scalar)==5) {
  for(unsigned j=0;j<8;++j)if(scanf("%x",&input[j])!=1)return 2;
  for(unsigned j=0;j<8;++j)if(scanf("%x",&merge[j])!=1)return 2;
  if(mode>3||imm>255||control>0xffff)return 2;
  uint32_t output[8],after_merge[8],saved,after_control,result;uint64_t flags,after_flags;
  unsigned char backing[13];memset(backing,0xa6,sizeof backing);unsigned char (*memory)[4]=(void *)(backing+1);memcpy(*memory,&scalar,4);
  __asm__ volatile("stmxcsr %0; pushfq; popq %1":"=m"(saved),"=r"(flags));flags=(flags&~UINT64_C(0x8d5))|(initial_flags&0x8d5);
  switch(imm) {
#include "dword_oracle_cases.inc"
  default:return 2;
  }
  if(memcmp(after_merge,merge,sizeof merge))return 3;
  for(unsigned j=0;j<sizeof backing;++j)if((j<1||j>=5)&&backing[j]!=0xa6)return 3;
  uint32_t memory_value;memcpy(&memory_value,*memory,4);
  printf("%08x %04x %03llx %08x",result,after_control,(unsigned long long)(after_flags&0x8d5),memory_value);
  for(unsigned j=0;j<8;++j)printf(" %08x",output[j]);putchar('\n');
 }
 return ferror(stdin)?1:0;
}
