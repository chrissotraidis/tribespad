#include "mmx.h"
#include <stdlib.h>
static unsigned lane(uint64_t v,unsigned i,unsigned width) {
 return (unsigned)((v >> (i*width)) & ((UINT64_C(1)<<width)-1));
}
static int signed16(unsigned v) { return (v&0x8000)?(int)v-65536:(int)v; }
static int64_t signed32(unsigned v) { return (v&0x80000000)?(int64_t)v-INT64_C(4294967296):v; }
uint64_t tb_mmx_eval(unsigned op,uint64_t a,uint64_t b) {
 uint64_t r=0; unsigned width,n; int64_t v;
 switch(op) {
 case TB_MOVQ:return b;
 case TB_PAND:return a&b;
 case TB_POR:return a|b;
 case TB_PXOR:return a^b;
 case TB_PADDB:case TB_PADDW:case TB_PSUBW:
  width=op==TB_PADDB?8:16;
  for(unsigned i=0;i<64/width;i++) {
   unsigned x=lane(a,i,width),y=lane(b,i,width);
   unsigned z=op==TB_PSUBW?x-y:x+y;
   r|=(uint64_t)(z&((1u<<width)-1))<<(i*width);
  } return r;
 case TB_PMULHW:
  for(unsigned i=0;i<4;i++) {
   /* Unsigned shift avoids implementation-defined negative right shift. */
   int32_t p=signed16(lane(a,i,16))*signed16(lane(b,i,16));
   r|=(uint64_t)(((uint32_t)p>>16)&65535)<<(16*i);
  } return r;
 case TB_PMADDWD:
  for(unsigned i=0;i<2;i++) {
   int64_t p=(int64_t)signed16(lane(a,2*i,16))*signed16(lane(b,2*i,16));
   p+=(int64_t)signed16(lane(a,2*i+1,16))*signed16(lane(b,2*i+1,16));
   r|=(uint64_t)(uint32_t)p<<(32*i);
  } return r;
 case TB_PUNPCKLBW:case TB_PUNPCKHBW:case TB_PUNPCKLWD:case TB_PUNPCKLDQ:
  width=(op==TB_PUNPCKLWD)?16:(op==TB_PUNPCKLDQ?32:8);
  n=32/width;
  for(unsigned i=0;i<n;i++) {
   unsigned j=i+(op==TB_PUNPCKHBW?n:0);
   r|=(uint64_t)lane(a,j,width)<<(2*i*width);
   r|=(uint64_t)lane(b,j,width)<<((2*i+1)*width);
  } return r;
 case TB_PACKUSWB:case TB_PACKSSDW:
  width=op==TB_PACKUSWB?8:16;n=32/width;
  for(unsigned i=0;i<2*n;i++) {
   uint64_t src=i<n?a:b;unsigned j=i%n;
   v=width==8?signed16(lane(src,j,16)):signed32(lane(src,j,32));
   int64_t lo=width==8?0:-32768,hi=width==8?255:32767;
   if(v<lo)v=lo;if(v>hi)v=hi;
   r|=((uint64_t)v&((1u<<width)-1))<<(i*width);
  } return r;
 case TB_PSRAW:case TB_PSRLW:case TB_PSLLW:case TB_PSRLD:case TB_PSLLD:
  width=(op==TB_PSRLD||op==TB_PSLLD)?32:16;
  for(unsigned i=0;i<64/width;i++) {
   uint64_t x=lane(a,i,width),z=0,mask=(UINT64_C(1)<<width)-1;
   if(op==TB_PSRAW) {
    if(b>=16)z=(x&0x8000)?65535:0;
    else if(b==0)z=x;
    else {z=x>>b;if(x&0x8000)z|=(mask<<(16-b))&mask;}
   } else if(b<width)z=(op==TB_PSLLW||op==TB_PSLLD)?(x<<b)&mask:x>>b;
   r|=z<<(i*width);
  } return r;
 case TB_PSRLQ:return b<64?a>>b:0;
 case TB_PSLLQ:return b<64?a<<b:0;
 default:abort(); /* Never silently approximate an unsupported operation. */
 }
}
