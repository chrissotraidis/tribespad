/* Differential check: the exact hardware fast path in tb_sse_evaluate must
   match the SoftFloat reference bit-for-bit, including status, for every
   input. Operands are drawn across the whole encoding space with extra weight
   on fast-path boundaries, specials, near-overflow and near-underflow. */
#include "../port/core/sse_arithmetic.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
TBSSEResult tb_sse_evaluate_reference(unsigned op,uint64_t a,uint64_t b,uint32_t control);
static uint64_t rng(uint64_t *s){uint64_t x=*s;x^=x<<13;x^=x>>7;x^=x<<17;return *s=x;}
static uint64_t pick32(uint64_t *s){
  uint64_t r=rng(s);uint32_t m=(uint32_t)(r>>9)&0x7fffff,sg=(uint32_t)(r>>40)&1;uint32_t e;
  switch((r>>41)%8){
    case 0:e=(uint32_t)(r>>44)%256;break;
    case 1:e=44+(uint32_t)(r>>44)%10;break;           /* low boundary */
    case 2:e=196+(uint32_t)(r>>44)%10;break;          /* high boundary */
    case 3:e=(r>>44)&1?0:255;if((r>>45)&1)m=0;break;  /* zero/denormal, inf/nan */
    case 4:m&=~((1u<<((r>>46)%23))-1);/* fallthrough: few significant bits */
    default:e=100+(uint32_t)(r>>44)%60;break;          /* common magnitudes */
  }
  return (sg<<31)|(e<<23)|m;
}
static uint64_t pick64(uint64_t *s){
  uint64_t r=rng(s),m=rng(s)&((UINT64_C(1)<<52)-1),sg=r>>63,e;
  switch(r%8){
    case 0:e=(r>>8)%2048;break;
    case 1:e=122+(r>>8)%12;break;
    case 2:e=1894+(r>>8)%12;break;
    case 3:e=(r>>8)&1?0:2047;if((r>>9)&1)m=0;break;
    case 4:m&=~((UINT64_C(1)<<((r>>10)%52))-1);/* fallthrough */
    default:e=900+(r>>8)%250;break;
  }
  return (sg<<63)|(e<<52)|m;
}
static const uint32_t controls[]={0x1f80,0x9fc0,0x0000,0x8040,0x3f80,0x5f80,0x7f80,0x1f00};
typedef struct{uint64_t seed,n,fast,bad;}Job;
static void *run(void *p){
  Job *j=p;uint64_t s=j->seed;
  for(uint64_t i=0;i<j->n;i++){
    unsigned op=(unsigned)(rng(&s)%6);uint32_t c=controls[rng(&s)%8];
    int single=op>=TB_SSE_MUL32;
    uint64_t a=single?pick32(&s)|((rng(&s)&0xffff)<<32):pick64(&s),b=single?pick32(&s):pick64(&s);
    if((rng(&s)&63)==0)b=a;                 /* x-x, x+x */
    if((rng(&s)&63)==1)b=a^(single?0x80000000u:UINT64_C(0x8000000000000000));
    TBSSEResult x=tb_sse_evaluate(op,a,b,c),y=tb_sse_evaluate_reference(op,a,b,c);
    if(x.valid!=y.valid||x.value!=y.value||x.raised!=y.raised){
      if(j->bad++<10)fprintf(stderr,"MISMATCH op %u ctl %x a %" PRIx64 " b %" PRIx64 " fast %" PRIx64 "/%x ref %" PRIx64 "/%x\n",op,c,a,b,x.value,x.raised,y.value,y.raised);
    }
  }
  return NULL;
}
int main(int argc,char **argv){
  uint64_t per=argc>1?strtoull(argv[1],0,10):1000000;enum{T=8};pthread_t t[T];Job j[T];
  for(int i=0;i<T;i++){j[i]=(Job){0x9e3779b97f4a7c15ull*(i+1),per,0,0};pthread_create(&t[i],0,run,&j[i]);}
  uint64_t bad=0;for(int i=0;i<T;i++){pthread_join(t[i],0);bad+=j[i].bad;}
  printf("%s: %" PRIu64 " cases, %" PRIu64 " mismatches\n",bad?"FAIL":"PASS",per*T,bad);return bad!=0;
}
