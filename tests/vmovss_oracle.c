/* Offline VEX reference through Rosetta. Not linked into the Apple client. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RUN(instruction) __asm__ volatile( \
 "ldmxcsr %[control]\n\t" \
 "vmovdqu %[a], %%ymm0\n\tvmovdqu %[b], %%ymm1\n\tvmovdqu %[c], %%ymm2\n\t" \
 "pushq %[flags]\n\tpopfq\n\t" instruction "\n\t" \
 "pushfq\n\tpopq %[after_flags]\n\tstmxcsr %[after_control]\n\t" \
 "vmovdqu %%ymm0, %[out0]\n\tvmovdqu %%ymm1, %[out1]\n\tvmovdqu %%ymm2, %[out2]\n\t" \
 "ldmxcsr %[saved]\n\tvzeroupper\n\t" \
 : [after_flags] "=&r"(after_flags), [after_control] "=m"(after_control), \
   [out0] "=m"(output[0]), [out1] "=m"(output[1]), [out2] "=m"(output[2]), [memory] "+m"(memory), [packed] "+m"(*packed) \
 : [control] "m"(control), [a] "m"(input[0]), [b] "m"(input[1]), [c] "m"(input[2]), \
   [flags] "r"(flags), [saved] "m"(saved) \
 : "ymm0","ymm1","ymm2","cc","memory")
int main(void) {
 unsigned mode,control,initial_flags,memory,immediate;
 while(scanf("%u %x %x %x",&mode,&control,&initial_flags,&memory)==4) {
  if(mode>75||control>0xffff||((mode>=4)&&(control&0x1f80)!=0x1f80))return 2;
  uint64_t input[3][4],output[3][4],flags,after_flags;uint32_t saved,after_control;
  for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++) {
   unsigned long long v;if(scanf("%llx",&v)!=1)return 3;input[i][j]=v;
  }
  if(scanf("%x",&immediate)!=1||immediate>255)return 3;
  _Alignas(32) unsigned char backing[65];unsigned char (*packed)[32]=(void *)(backing+((mode>=23&&mode<=30)||mode>=40?1:0));
  memcpy(*packed,input[2],32);
  if(mode==17||mode==22||mode==26||mode==30||mode==35||mode==39||mode==43||mode==47||mode==53)for(unsigned i=0;i<((mode==22||mode==30||mode==39||mode==47)?32:16);i++)(*packed)[i]^=0xff;
  if(mode==58||mode==59)for(unsigned i=0;i<8;i++)(*packed)[i]^=0xff;
  __asm__ volatile("stmxcsr %0; pushfq; popq %1":"=m"(saved),"=r"(flags));
  flags=(flags&~UINT64_C(0x8d5))|(initial_flags&0x8d5);
  switch(mode) {
  case 0:RUN("vmovss %%xmm2, %%xmm1, %%xmm0");break;
  case 1:RUN(".byte 0xc5,0xf2,0x11,0xd0");break;
  case 2:RUN("vmovss %[memory], %%xmm0");break;
  case 3:RUN("vmovss %%xmm2, %[memory]");break;
  case 4:RUN("vmulss %%xmm2, %%xmm1, %%xmm0");break;
  case 5:RUN("vaddss %%xmm2, %%xmm1, %%xmm0");break;
  case 6:RUN("vcvtss2sd %%xmm2, %%xmm1, %%xmm0");break;
  case 7:RUN("vxorpd %%xmm2, %%xmm1, %%xmm0");break;
  case 8:RUN("vucomisd %%xmm2, %%xmm1");break;
  case 9:RUN("vsqrtsd %%xmm2, %%xmm1, %%xmm0");break;
  case 10:RUN("vcvtsd2ss %%xmm2, %%xmm1, %%xmm0");break;
  case 11:RUN("vcomiss %%xmm2, %%xmm1");break;
  case 12:RUN("vxorps %%xmm2, %%xmm1, %%xmm0");break;
  case 13:RUN("vdivss %%xmm2, %%xmm1, %%xmm0");break;
  case 14:RUN("vmovaps %%xmm2, %%xmm0");break;
  case 15:RUN(".byte 0xc5,0xf8,0x29,0xd0");break;
  case 16:RUN("vmovaps %[packed], %%xmm0");break;
  case 17:RUN("vmovaps %%xmm2, %[packed]");break;
  case 18:RUN("vsubss %%xmm2, %%xmm1, %%xmm0");break;
  case 19:RUN("vmovaps %%ymm2, %%ymm0");break;
  case 20:RUN(".byte 0xc5,0xfc,0x29,0xd0");break;
  case 21:RUN("vmovaps %[packed], %%ymm0");break;
  case 22:RUN("vmovaps %%ymm2, %[packed]");break;
  case 23:RUN("vmovups %%xmm2, %%xmm0");break;
  case 24:RUN(".byte 0xc5,0xf8,0x11,0xd0");break;
  case 25:RUN("vmovups %[packed], %%xmm0");break;
  case 26:RUN("vmovups %%xmm2, %[packed]");break;
  case 27:RUN("vmovups %%ymm2, %%ymm0");break;
  case 28:RUN(".byte 0xc5,0xfc,0x11,0xd0");break;
  case 29:RUN("vmovups %[packed], %%ymm0");break;
  case 30:RUN("vmovups %%ymm2, %[packed]");break;
  case 31:RUN("vzeroupper");break;
  case 32:RUN("vmovdqa %%xmm2, %%xmm0");break;
  case 33:RUN(".byte 0xc5,0xf9,0x7f,0xd0");break;
  case 34:RUN("vmovdqa %[packed], %%xmm0");break;
  case 35:RUN("vmovdqa %%xmm2, %[packed]");break;
  case 36:RUN("vmovdqa %%ymm2, %%ymm0");break;
  case 37:RUN(".byte 0xc5,0xfd,0x7f,0xd0");break;
  case 38:RUN("vmovdqa %[packed], %%ymm0");break;
  case 39:RUN("vmovdqa %%ymm2, %[packed]");break;
  case 40:RUN("vmovdqu %%xmm2, %%xmm0");break;
  case 41:RUN(".byte 0xc5,0xfa,0x7f,0xd0");break;
  case 42:RUN("vmovdqu %[packed], %%xmm0");break;
  case 43:RUN("vmovdqu %%xmm2, %[packed]");break;
  case 44:RUN("vmovdqu %%ymm2, %%ymm0");break;
  case 45:RUN(".byte 0xc5,0xfe,0x7f,0xd0");break;
  case 46:RUN("vmovdqu %[packed], %%ymm0");break;
  case 47:RUN("vmovdqu %%ymm2, %[packed]");break;
#include "vshufps_oracle.inc"
#include "vextractf128_oracle.inc"
  case 54:RUN("vmovhlps %%xmm2, %%xmm1, %%xmm0");break;
  case 55:RUN("vmovlhps %%xmm2, %%xmm1, %%xmm0");break;
  case 56:RUN("vmovlps %[packed], %%xmm1, %%xmm0");break;
  case 57:RUN("vmovhps %[packed], %%xmm1, %%xmm0");break;
  case 58:RUN("vmovlps %%xmm2, %[packed]");break;
  case 59:RUN("vmovhps %%xmm2, %[packed]");break;
  case 60:RUN("vmulps %%xmm2, %%xmm1, %%xmm0");break;
  case 61:RUN("vmulps %%ymm2, %%ymm1, %%ymm0");break;
  case 62:RUN("vmulps %[packed], %%xmm1, %%xmm0");break;
  case 63:RUN("vmulps %[packed], %%ymm1, %%ymm0");break;
  case 64:RUN("vsubps %%xmm2, %%xmm1, %%xmm0");break;
  case 65:RUN("vsubps %%ymm2, %%ymm1, %%ymm0");break;
  case 66:RUN("vsubps %[packed], %%xmm1, %%xmm0");break;
  case 67:RUN("vsubps %[packed], %%ymm1, %%ymm0");break;
  case 68:RUN("vaddps %%xmm2, %%xmm1, %%xmm0");break;
  case 69:RUN("vaddps %%ymm2, %%ymm1, %%ymm0");break;
  case 70:RUN("vaddps %[packed], %%xmm1, %%xmm0");break;
  case 71:RUN("vaddps %[packed], %%ymm1, %%ymm0");break;
  case 72:RUN("vdivps %%xmm2, %%xmm1, %%xmm0");break;
  case 73:RUN("vdivps %%ymm2, %%ymm1, %%ymm0");break;
  case 74:RUN("vdivps %[packed], %%xmm1, %%xmm0");break;
  case 75:RUN("vdivps %[packed], %%ymm1, %%ymm0");break;
  default:return 2;

  }
  for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++)printf("%016llx ",(unsigned long long)output[i][j]);
  uint64_t capture[4];memcpy(capture,*packed,32);
  printf("%08x %08x %03llx %016llx %016llx %016llx %016llx\n",memory,after_control,(unsigned long long)(after_flags&0x8d5),(unsigned long long)capture[0],(unsigned long long)capture[1],(unsigned long long)capture[2],(unsigned long long)capture[3]);
 }
 return ferror(stdin)?1:0;
}
