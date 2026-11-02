/* Tribes: Vengeance's x87 fast paths (port/vengeance/vengeance_float80.c) against Bochs SoftFloat:
   random binary32 operands at PC=24 and binary64 operands at PC=53, and binary64
   values stored as binary32; every
   result and its precision/C1 flags must match. Run: scripts/test-x87-fast.py */
#include "../port/vengeance/vengeance_float80.c"
#include <stdio.h>
#include <stdlib.h>
static uint32_t rng=12345;static uint32_t rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static float rf(void){for(;;){uint32_t b=rnd();int k=rnd()%4;if(k==0)b=(b&0x80000000)|((uint32_t)(100+rnd()%55)<<23)|(b&0x7fffff); /* moderate */
 float f;memcpy(&f,&b,4);if(isfinite(f)&&(f==0||fabsf(f)>=0x1p-126f))return f;}}
static TBExt80 ext(float f){unsigned fl=0;uint32_t b;memcpy(&b,&f,4);return widen(b>>31,(b>>23)&0xff,b&0x7fffff,8,23,&fl);}
static uint64_t r64(void){return (uint64_t)rnd()<<32|rnd();}
static double rd(void){for(;;){uint64_t b=r64();if(rnd()%3==0)b=(b&UINT64_C(0x800fffffffffffff))|((uint64_t)(900+rnd()%246)<<52);
 if(rnd()%4==0)b&=~UINT64_C(0x1fffffff); /* binary32-like values */
 double d;memcpy(&d,&b,8);if(isfinite(d)&&(d==0||fabs(d)>=0x1p-1022))return d;}}
static TBExt80 extd(double d){unsigned fl=0;uint64_t b;memcpy(&b,&d,8);return widen((uint32_t)(b>>63),(uint32_t)(b>>52)&0x7ff,b&UINT64_C(0xfffffffffffff),11,52,&fl);}
static unsigned long check_double(void){unsigned long fast=0,bad=0;
 for(long i=0;i<3000000;i++){unsigned op=rnd()%5;double x=rd(),y=rd();if(rnd()%50==0)y=x;if(rnd()%50==0)y=-x;if(rnd()%50==0)y=0;
  TBExt80 a=extd(x),b=extd(y),zf,zb;uint16_t ff=0,fb=0;
  if(!double_fast(op,a,b,0x027f,&zf,&ff))continue;fast++;
  if(!tb_bo_arithmetic(op,a,b,0x027f,&zb,&fb)){printf("bochs refused\n");exit(1);}
  if(zf.significand!=zb.significand||zf.sign_exponent!=zb.sign_exponent||(ff&0x220)!=(fb&0x220)){if(bad++<10)printf("double op %u x %a y %a fast %04x:%016llx f%x bochs %04x:%016llx f%x\n",op,x,y,zf.sign_exponent,(unsigned long long)zf.significand,ff,zb.sign_exponent,(unsigned long long)zb.significand,fb);}
 }
 /* stores of binary64 values to binary32 */
 for(long i=0;i<3000000;i++){double x=rd();if(rnd()%2)x=ldexp(x,-(int)(rnd()%40));TBExt80 a=extd(x);uint64_t zf,zb;uint16_t ff=0,fb=0;
  if(!vg_store_real(32,a,0x027f,&zf,&ff))continue;
  if(!tb_bo_store_real(32,a,0x027f,&zb,&fb)){printf("bochs refused store\n");exit(1);}
  if(zf!=zb||(ff&0x23f)!=(fb&0x23f)){if(bad++<20)printf("store x %a fast %08llx f%x bochs %08llx f%x\n",x,(unsigned long long)zf,ff,(unsigned long long)zb,fb);}
 }
 printf("double fast %lu mismatches %lu\n",fast,bad);return bad;}
int main(void){unsigned long tested=0,fast=0,bad=check_double();
 uint16_t cws[]={0x007f,0x027f};
 for(int c=0;c<2;c++)for(long i=0;i<3000000;i++){unsigned op=rnd()%5;float x=rf(),y=rf();if(rnd()%50==0)y=x;if(rnd()%50==0)y=-x;if(rnd()%50==0)y=0;
  TBExt80 a=ext(x),b=ext(y),zf,zb;uint16_t ff=0,fb=0;tested++;
  if(!single_fast(op,a,b,cws[c],&zf,&ff))continue;fast++;
  if(!tb_bo_arithmetic(op,a,b,cws[c],&zb,&fb)){printf("bochs refused\n");return 1;}
  fb&=0x23f|0x200;
  if(zf.significand!=zb.significand||zf.sign_exponent!=zb.sign_exponent||(ff&0x220)!=(fb&0x220)){if(bad++<10)printf("op %u cw %x x %a y %a fast %04x:%016llx f%x bochs %04x:%016llx f%x\n",op,cws[c],x,y,zf.sign_exponent,(unsigned long long)zf.significand,ff,zb.sign_exponent,(unsigned long long)zb.significand,fb);}
 }
 printf("tested %lu fast %lu mismatches %lu\n",tested,fast,bad);return bad!=0;}
uint32_t halopad_x87_result(_cpu *c, const int64_t *v){(void)c;(void)v;return 0;}
