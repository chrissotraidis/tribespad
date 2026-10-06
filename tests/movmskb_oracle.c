/* Offline VEX reference through Rosetta. Not linked into the Apple client. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RUN(instruction) __asm__ volatile( \
 "ldmxcsr %[control]\n\t" \
 "vmovdqu %[a], %%ymm0\n\tvmovdqu %[b], %%ymm1\n\tvmovdqu %[c], %%ymm2\n\t" \
 "movq $-1, %%rax\n\t" \
 "pushq %[flags]\n\tpopfq\n\t" instruction "\n\t" \
 "movq %%rax, %[dest]\n\t" \
 "pushfq\n\tpopq %[after_flags]\n\tstmxcsr %[after_control]\n\t" \
 "vmovdqu %%ymm0, %[out0]\n\tvmovdqu %%ymm1, %[out1]\n\tvmovdqu %%ymm2, %[out2]\n\t" \
 "ldmxcsr %[saved]\n\tvzeroupper\n\t" \
 : [after_flags] "=&r"(after_flags), [after_control] "=m"(after_control), \
   [dest] "=m"(dest), [out0] "=m"(output[0]), [out1] "=m"(output[1]), [out2] "=m"(output[2]), [memory] "+m"(memory), [packed] "+m"(*packed) \
 : [control] "m"(control), [a] "m"(input[0]), [b] "m"(input[1]), [c] "m"(input[2]), \
   [flags] "r"(flags), [saved] "m"(saved) \
 : "rax","ymm0","ymm1","ymm2","cc","memory")
int main(void) {
 unsigned mode,control,initial_flags,memory,immediate;
 while(scanf("%u %x %x %x",&mode,&control,&initial_flags,&memory)==4) {
  if(mode>1||control>0xffff||((control&0x1f80)!=0x1f80))return 2;
  uint64_t input[3][4],output[3][4],flags,after_flags,dest;uint32_t saved,after_control;
  for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++) {
   unsigned long long v;if(scanf("%llx",&v)!=1)return 3;input[i][j]=v;
  }
  if(scanf("%x",&immediate)!=1||immediate>255)return 3;
  _Alignas(32) unsigned char backing[65];unsigned char (*packed)[32]=(void *)(backing+1);
  memcpy(*packed,input[2],32);
  __asm__ volatile("stmxcsr %0; pushfq; popq %1":"=m"(saved),"=r"(flags));
  flags=(flags&~UINT64_C(0x8d5))|(initial_flags&0x8d5);
  switch(mode) {
  case 0:RUN("vpmovmskb %%xmm1, %%eax");break;
  case 1:RUN("vpmovmskb %%ymm1, %%eax");break;
  }
  for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++)printf("%016llx ",(unsigned long long)output[i][j]);
  uint64_t capture[4];memcpy(capture,*packed,32);
  printf("%08x %08x %03llx %016llx %016llx %016llx %016llx %016llx\n",memory,after_control,(unsigned long long)(after_flags&0x8d5),(unsigned long long)capture[0],(unsigned long long)capture[1],(unsigned long long)capture[2],(unsigned long long)capture[3],(unsigned long long)dest);
 }
 return ferror(stdin)?1:0;
}
