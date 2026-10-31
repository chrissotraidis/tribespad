/* Tribes: Vengeance's x87 runtime: TribesPad's (port/llasm-support/tribes_float80.c,
   compiled here in its place for this game only), with two exact fast paths ahead
   of Bochs SoftFloat:

   - At single precision (PC=24) and round to nearest, two binary32 operands
     (normal or zero) give the x87 result through host binary32 arithmetic
     whenever that result is an exact zero or a normal binary32 number of
     magnitude at least 2^-100: x87 rounds the same exact value to the same 24
     bits and nothing over- or underflows. The exact error term (TwoSum, FMA
     residual) sets precision and the round-up bit C1, as Bochs does. Direct3D
     leaves the FPU at single precision, so most of the game's x87 work qualifies.
   - Storing a value that already is a binary32 or binary64 is exact and raises nothing.

   On the iPad, SoftFloat was about a third of each frame. Checked against Bochs
   on random operands by scripts/test-x87-single-fast.py. */
#include <math.h>
#include "../core/bochs_math.h"
static int vg_arithmetic(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,TBExt80 *result,uint16_t *flags);
static int vg_store_real(unsigned width,TBExt80 value,uint16_t control,uint64_t *bits,uint16_t *flags);
#define tb_bo_arithmetic vg_arithmetic
#define tb_bo_store_real vg_store_real
#include "../llasm-support/tribes_float80.c"
#undef tb_bo_arithmetic
#undef tb_bo_store_real

static int as_float(TBExt80 v,float *out){
 uint32_t sign=(uint32_t)(v.sign_exponent>>15),e=v.sign_exponent&0x7fff,bits;
 if(!e&&!v.significand){bits=sign<<31;memcpy(out,&bits,4);return 1;}
 if(!(v.significand>>63)||(v.significand&UINT64_C(0xffffffffff)))return 0;
 int32_t u=(int32_t)e-16383;if(u<-126||u>127)return 0;
 bits=sign<<31|(uint32_t)(u+127)<<23|(uint32_t)((v.significand>>40)&0x7fffff);memcpy(out,&bits,4);return 1;
}
static int as_double_bits(TBExt80 v,uint64_t *out){
 uint64_t sign=(uint64_t)(v.sign_exponent>>15),e=v.sign_exponent&0x7fff;
 if(!e&&!v.significand){*out=sign<<63;return 1;}
 if(!(v.significand>>63)||(v.significand&0x7ff))return 0;
 int64_t u=(int64_t)e-16383;if(u<-1022||u>1023)return 0;
 *out=sign<<63|(uint64_t)(u+1023)<<52|((v.significand<<1)>>12);return 1;
}
static int single_fast(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,TBExt80 *result,uint16_t *flags){
 if(op>4||(control&0x0f00)||!x87_exact_host_nearest())return 0;
 float x,y=0,z,err,direction;if(!as_float(a,&x)||(op!=4&&!as_float(b,&y)))return 0;
 switch(op){
 case 0:case 1:{if(op==1)y=-y;z=x+y;float v=z-x;err=(x-(z-v))+(y-v);direction=err;break;}
 case 2:z=x*y;err=fmaf(x,y,-z);direction=err;break;
 case 3:if(y==0||(x!=0&&!(fabsf(x)>=0x1p-100f)))return 0;z=x/y;err=fmaf(-z,y,x);direction=y<0?-err:err;break;
 default:if(!(x>=0x1p-100f))return 0;z=sqrtf(x);err=fmaf(-z,z,x);direction=err;break;
 }
 uint32_t bits;memcpy(&bits,&z,4);
 if(z==0){if(err!=0||(op==2&&x!=0&&y!=0)||(op==3&&x!=0))return 0; /* an underflow, not a true zero */
  result->significand=0;result->sign_exponent=(uint16_t)((bits>>31)<<15);*flags=0;return 1;}
 if(!(fabsf(z)>=0x1p-100f)||!(fabsf(z)<=0x1.fffffep127f))return 0;
 result->significand=UINT64_C(1)<<63|(uint64_t)(bits&0x7fffff)<<40;
 result->sign_exponent=(uint16_t)((bits>>31)<<15|(((bits>>23)&0xff)-127+16383));
 *flags=err==0?0:(uint16_t)(0x20|((direction<0)==(z>0)?0x200:0));return 1;
}
static int vg_arithmetic(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,TBExt80 *result,uint16_t *flags){
 return single_fast(op,a,b,control,result,flags)||tb_bo_arithmetic(op,a,b,control,result,flags);
}
static int vg_store_real(unsigned width,TBExt80 value,uint16_t control,uint64_t *bits,uint16_t *flags){
 float s;
 if(width==32&&as_float(value,&s)){uint32_t b;memcpy(&b,&s,4);*bits=b;*flags=0;return 1;}
 if(width==64&&as_double_bits(value,bits)){*flags=0;return 1;}
 return tb_bo_store_real(width,value,control,bits,flags);
}
