#include "../port/llasm-support/llasm_cpu.h"
#include "../port/llasm-support/tribes_sse.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cases.inc"
int main(void){unsigned long total=0;unsigned roles[][3]={{0,1,2},{0,0,1},{0,1,0},{0,1,1},{0,0,0},{7,6,5},{7,7,6},{7,6,7}};
for(unsigned i=0;i<sizeof corpus/sizeof corpus[0];++i)for(unsigned r=0;r<8;++r)for(unsigned memory=0;memory<2;++memory){const sample *q=&corpus[i];unsigned d=roles[r][0],m=roles[r][1],s=roles[r][2];_cpu c;for(unsigned j=0;j<sizeof c;++j)((unsigned char *)&c)[j]=(unsigned char)(j*37+i*13+r);c._mxcsr=q->control;unsigned char backing[9];memset(backing,0xa6,sizeof backing);memcpy(backing+1,&q->a,4);c._pointer_offset=(uintptr_t)(backing+1)-0x1003;if(!memory)memcpy(c._xmm[s],&q->a,4);_cpu expected=c;memcpy(expected._xmm[d],c._xmm[m],16);memcpy(expected._xmm[d],&q->result0,8);memset(expected._ymm_hi[d],0,16);unsigned e=(q->a>>23)&255,f=q->a&0x7fffff;unsigned raised=e==255&&f&&!(f&0x400000)?1:!e&&f&&!(q->control&0x40)?2:0;expected._mxcsr|=raised;if(memory)tb_avx_ss2sdmem(&c,d,m,0x1003);else tb_avx_ss2sd(&c,d,m,s);assert(!memcmp(&c,&expected,sizeof c));assert(backing[0]==0xa6&&backing[5]==0xa6);++total;}
printf("PASS: %lu scalar widening helper regression checks; aliases, merge, upper clearing and MXCSR\n",total);return 0;}
