/* Differential check of port/core/x87_exact.h against the game's actual
   x87 path (Bochs SoftFloat via tb_bo_arithmetic): whenever the exact path
   accepts an input, value and flags must match for every precision control. */
#include "../port/core/x87_exact.h"
#include "../port/core/bochs_math.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
static uint64_t s=0x9e3779b97f4a7c15ull;
static uint64_t rng(void){s^=s<<13;s^=s>>7;s^=s<<17;return s;}
static TBExt80 pick(void){
  uint64_t r=rng();TBExt80 v;unsigned kind=r%10;int e;uint64_t m=rng()|(UINT64_C(1)<<63);
  switch(kind){
   case 0: m&=~((UINT64_C(1)<<40)-1); e=(int)(rng()%60)-30; break;          /* float-sized */
   case 1: m&=~UINT64_C(0x7ff); e=(int)(rng()%200)-100; break;               /* double-sized */
   case 2: m&=~((UINT64_C(1)<<(rng()%63))-1); m|=UINT64_C(1)<<63; e=(int)(rng()%40)-20; break; /* few bits */
   case 3: return (TBExt80){0,(uint16_t)((rng()&1)<<15)};                       /* zeros */
   case 4: e=(int)(rng()%2100)-1050; m&=~UINT64_C(0x7ff); break;               /* range edges */
   case 5: return (TBExt80){rng(),(uint16_t)(rng())};                           /* anything */
   case 6: m=(uint64_t)(rng()%5000+1); { int lz=__builtin_clzll(m); m<<=lz; e=63-lz; } break; /* small ints */
   default: e=(int)(rng()%64)-32; break;                                         /* full 64-bit */
  }
  v.significand=m;v.sign_exponent=(uint16_t)(((rng()&1)<<15)|(uint16_t)(e+16383));return v;
}
int main(int argc,char **argv){
  uint64_t n=argc>1?strtoull(argv[1],0,10):2000000,hit=0,bad=0;
  const uint16_t controls[]={0x37f,0x27f,0x07f,0x77f,0xb7f,0xf7f,0x372};
  for(uint64_t i=0;i<n;i++){
    unsigned op=rng()%5;uint16_t c=controls[rng()%7];TBExt80 a=pick(),b=pick();
    if(rng()%16==0)b=a;
    TBExt80 fast,ref;uint16_t flags;
    if(!x87_exact_arithmetic(op,a,b,c,&fast))continue;
    hit++;
    if(!tb_bo_arithmetic(op,a,b,c,&ref,&flags)){bad++;continue;}
    if(ref.significand!=fast.significand||ref.sign_exponent!=fast.sign_exponent||flags){
      if(bad++<10)fprintf(stderr,"MISMATCH op %u cw %x a %04x:%016" PRIx64 " b %04x:%016" PRIx64 " fast %04x:%016" PRIx64 " ref %04x:%016" PRIx64 " flags %x\n",
        op,c,a.sign_exponent,a.significand,b.sign_exponent,b.significand,fast.sign_exponent,fast.significand,ref.sign_exponent,ref.significand,flags);
    }
  }
  printf("%s: %" PRIu64 " cases, %" PRIu64 " exact-path hits, %" PRIu64 " mismatches\n",bad?"FAIL":"PASS",n,hit,bad);
  return bad!=0;
}
