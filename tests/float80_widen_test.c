/* Exhaustive binary32 and edge/random binary64 widening against SoftFloat. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define LITTLEENDIAN 1
#include "softfloat.h"
typedef struct { uint64_t significand; uint16_t sign_exponent; } TBExt80;
TBExt80 tribes_float80_widen32_for_test(uint32_t,unsigned *);
TBExt80 tribes_float80_widen64_for_test(uint64_t,unsigned *);
static unsigned map(unsigned f){return ((f&16)?1:0)|((f&8)?4:0)|((f&4)?8:0)|((f&2)?16:0)|((f&1)?32:0);}
static uint64_t state=0x9e3779b97f4a7c15ull;static uint64_t next(void){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
int main(void){
 uint64_t checked=0;
 for(uint64_t b=0;b<=0xffffffffull;b++){
  softfloat_exceptionFlags=0;extFloat80_t z=f32_to_extF80((float32_t){(uint32_t)b});unsigned want=map(softfloat_exceptionFlags),got;
  TBExt80 v=tribes_float80_widen32_for_test((uint32_t)b,&got);
  if(v.significand!=z.signif||v.sign_exponent!=z.signExp||got!=want){printf("FAIL32 %08llx\n",(unsigned long long)b);return 1;}checked++;
 }
 uint64_t edges[]={0,1,0xfffffffffffffull,0x10000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0x7ff0000000000001ull,0x7ff8000000000000ull,0x7fffffffffffffffull,0x8000000000000000ull,0x8000000000000001ull,0xfff0000000000000ull,0xfff4000000000000ull,0x3ff0000000000000ull};
 for(unsigned i=0;i<sizeof edges/sizeof *edges+200000000u;i++){
  uint64_t b=i<sizeof edges/sizeof *edges?edges[i]:next();
  if((i&3)==1&&i>=sizeof edges/sizeof *edges)b&=0x800fffffffffffffull; /* subnormals */
  if((i&3)==2&&i>=sizeof edges/sizeof *edges)b|=0x7ff0000000000000ull; /* NaN/inf */
  softfloat_exceptionFlags=0;extFloat80_t z=f64_to_extF80((float64_t){b});unsigned want=map(softfloat_exceptionFlags),got;
  TBExt80 v=tribes_float80_widen64_for_test(b,&got);
  if(v.significand!=z.signif||v.sign_exponent!=z.signExp||got!=want){printf("FAIL64 %016llx\n",(unsigned long long)b);return 1;}checked++;
 }
 printf("PASS: %llu widenings identical to SoftFloat (all binary32, binary64 edges + 200M random incl. subnormal/NaN)\n",(unsigned long long)checked);return 0;
}

