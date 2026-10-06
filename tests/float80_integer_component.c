/* Command-line component; expected values come from the independent x86 oracle. */
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern void x87_fninit_void(CPU);
extern int32_t x87_fist_int32(CPU);
extern int16_t x87_fistp_int16(CPU);
extern int32_t x87_fistp_int32(CPU);
extern uint32_t x87_fistp_int64(CPU);
int main(int argc,char **argv){
 assert(argc==2&&sizeof(_cpu)==440);FILE *f=fopen(argv[1],"r");assert(f);unsigned cases=0;
 unsigned width,pop,cw,seed,empty,exponent,sw;unsigned long long m,bits;
 for(;;){
  int n=fscanf(f,"%u %u %x %x %u %llx %x %llx %x",&width,&pop,&cw,&seed,&empty,&m,&exponent,&bits,&sw);
  if(n==EOF)break;assert(n==9);
  _cpu c;memset(&c,0xa5,sizeof c);x87_fninit_void(&c);c._st_top=7;c._st_cw=cw;c._st_sw_cond=seed;c._st_tag=empty?0xffff:0x3fff;
  uint64_t significand=m;memcpy(c._st+7,&significand,8);c._st_exp[7]=(uint16_t)exponent;
  uint64_t buffer=UINT64_C(0xa5a5a5a5a5a5a5a5);c._pointer_offset=(uintptr_t)&buffer-0x500000;_cpu before=c;
  uint64_t actual;
  if(width==16&&pop)actual=(uint16_t)x87_fistp_int16(&c);
  else if(width==32)actual=(uint32_t)(pop?x87_fistp_int32(&c):x87_fist_int32(&c));
  else {assert(width==64&&pop);assert(x87_fistp_int64(&c)==0x500000);actual=buffer;}
  assert(actual==bits&&((c._st_sw_cond|(c._st_top<<11))&0xbaff)==(sw&0xbaff));
  assert(c._st_top==(pop?0u:7u)&&c._st_tag==((pop||empty)?0xffffu:0x3fffu));
  if(width!=64)assert(buffer==UINT64_C(0xa5a5a5a5a5a5a5a5));
  c._st_top=before._st_top;c._st_sw_cond=before._st_sw_cond;c._st_tag=before._st_tag;
  if(width==64)c._st_result=before._st_result;
  assert(!memcmp(&c,&before,sizeof c));cases++;
 }
 assert(!ferror(f)&&!fclose(f)&&cases==33225);printf("PASS:%u independent integer-store vectors; full unrelated CPU state preserved\n",cases);return 0;
}
