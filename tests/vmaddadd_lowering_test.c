#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "moves.inc"
_Static_assert(sizeof(_cpu)==440,"Full native CPU ABI");
static uint32_t state=0x13b754ef;
static uint32_t random_word(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
int main(void) {
 unsigned long long count=0;
 for(unsigned block=0;block<sizeof moves/sizeof moves[0];block++)for(unsigned iteration=0;iteration<256;iteration++) {
  const move_case *c=&moves[block];_cpu cpu,expected;unsigned char backing[97],wanted[97],a[32],b[32],result[32];unsigned char *memory=backing+17;
  for(unsigned j=0;j<sizeof cpu;j++)((unsigned char *)&cpu)[j]=(unsigned char)random_word();
  for(unsigned j=0;j<sizeof backing;j++)backing[j]=(unsigned char)random_word();
  cpu._mxcsr=(block*256+iteration)&0xffff;unsigned n=c->wide?32:16,offset=c->address==3?3:c->address==5?0:iteration%(65-n);
  uint64_t av[4],bv[4];
  const uint32_t edge[]={0,1,UINT32_MAX,0x80000000,0x7fffffff,0x80008000,0x7fff7fff,0xffff0001,0x00ff00ff,0x7f807f80,0xffffffff,0x01010101};
  for(unsigned lane=0;lane<4;lane++) {
   uint32_t x=iteration<144?edge[(iteration/12+lane)%12]:random_word(),y=iteration<144?edge[(iteration+lane)%12]:random_word();
   av[lane]=(uint64_t)x|((uint64_t)(iteration<144?edge[(iteration/12+lane+1)%12]:random_word())<<32);
   bv[lane]=(uint64_t)y|((uint64_t)(iteration<144?edge[(iteration+lane+1)%12]:random_word())<<32);
  }
  if(c->op==0 && iteration>=144 && iteration<158) {
   static const unsigned char pairs[][4]={{255,127,255,127},{255,128,255,128},{190,2,255,127},{127,3,255,127},{212,3,253,127},{255,2,254,127},{192,2,255,127},{77,5,255,127},{125,255,255,128},{126,255,255,128},{127,255,255,128},{128,255,255,128},{192,254,255,129},{130,255,255,128}};
   const unsigned char *p=pairs[iteration-144];
   for(unsigned i=0;i<32;i+=2){((unsigned char *)av)[i]=p[0];((unsigned char *)av)[i+1]=p[2];((unsigned char *)bv)[i]=p[1];((unsigned char *)bv)[i+1]=p[3];}
  }
  memcpy(cpu._xmm[c->left],av,16);if(c->wide)memcpy(cpu._ymm_hi[c->left],av+2,16);
  cpu._pointer_offset=(uintptr_t)memory-0x200000;
  if(c->kind) {
   memcpy(memory+offset,bv,n);
   if(c->address==0)cpu._eax=0x200000+offset;
   else if(c->address==1)cpu._esp=0x200000+offset-c->disp;
   else if(c->address==2)cpu._ebx=0x200000+offset-cpu._esi*4+17;
   else if(c->address==4)cpu._ecx=0x200000+offset;
   else if(c->address==5)cpu._pointer_offset=(uintptr_t)memory-c->disp;
   memcpy(b,memory+offset,n);
  } else {
   memcpy(cpu._xmm[c->right],bv,16);if(c->wide)memcpy(cpu._ymm_hi[c->right],bv+2,16);
   memcpy(b,cpu._xmm[c->right],16);if(c->wide)memcpy(b+16,cpu._ymm_hi[c->right],16);
  }
  memcpy(a,cpu._xmm[c->left],16);if(c->wide)memcpy(a+16,cpu._ymm_hi[c->left],16);
  for(unsigned i=0;i<n;) {
   if(c->op==0) {
    int8_t y0,y1;memcpy(&y0,b+i,1);memcpy(&y1,b+i+1,1);
    int64_t sum=(int64_t)a[i]*y0+(int64_t)a[i+1]*y1;
    int16_t value=(int16_t)(sum>INT16_MAX?INT16_MAX:sum<INT16_MIN?INT16_MIN:sum);memcpy(result+i,&value,2);i+=2;
   } else if(c->op==1) {
    int16_t x0,x1,y0,y1;memcpy(&x0,a+i,2);memcpy(&x1,a+i+2,2);memcpy(&y0,b+i,2);memcpy(&y1,b+i+2,2);
    int64_t sum=(int64_t)x0*y0+(int64_t)x1*y1;uint32_t value=(uint32_t)((uint64_t)sum&UINT32_MAX);memcpy(result+i,&value,4);i+=4;
   } else if(c->op==2) {
    uint32_t x,y;memcpy(&x,a+i,4);memcpy(&y,b+i,4);uint32_t value=(uint32_t)((uint64_t)x+y);memcpy(result+i,&value,4);i+=4;
   } else {result[i]=(unsigned char)(((unsigned)a[i]+b[i])&255);i++;}
  }
  expected=cpu;memcpy(wanted,backing,sizeof wanted);memcpy(expected._xmm[c->dest],result,16);
  if(c->wide)memcpy(expected._ymm_hi[c->dest],result+16,16);else memset(expected._ymm_hi[c->dest],0,16);
  bodies[block](&cpu);assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(backing,wanted,sizeof backing));count++;
 }
 printf("PASS: %llu strict emitted VPMADDUBSW/VPMADDWD/VPADDD/VPADDB cases; all39 original byte sites, all aliases/widths/C5/C4 WIG, guarded unaligned memory, full CPU/flags and all65536 MXCSR values\n",count);return 0;
}
