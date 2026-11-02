/* Tribes: Vengeance's x87 runtime: TribesPad's (port/llasm-support/tribes_float80.c,
   compiled here in its place for this game only), with two exact fast paths ahead
   of Bochs SoftFloat:

   - At double precision (PC=53, where Unreal keeps the FPU) and at single
     precision (PC=24), round to nearest, two operands of that format (normal
     or zero) give the x87 result through host arithmetic in that format
     whenever the result is an exact zero or a normal number well inside the
     format's range: x87 rounds the same exact value to the same 53 or 24 bits
     and nothing over- or underflows. The exact error term (TwoSum, FMA
     residual) sets precision and the round-up bit C1, as Bochs does.
   - A store of a binary64 value as binary64 or binary32 (round to nearest) is
     a host conversion: one rounding of the same exact value.

   On the iPad, SoftFloat was about a third of each frame. Checked against Bochs
   on random operands by scripts/test-x87-fast.py. */
#if !VENGEANCE_X87_DOUBLE  /* vengeance_float64.c replaces this runtime */
#include <math.h>
#include "../core/bochs_math.h"
#include "../core/x87_exact.h"
/* tribes_float80.c tries x87_exact.h's exact path before SoftFloat; at PC=53 and
   PC=24 with round to nearest the fast paths below cover it (and the inexact
   results too), so that check is skipped there instead of done twice. */
static int vg_exact_arithmetic(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,TBExt80 *result){
 unsigned pc=(control>>8)&3;
 if(op<=4&&!(control&0x0c00)&&(pc==0||pc==2))return 0;
 return x87_exact_arithmetic(op,a,b,control,result);
}
static int vg_arithmetic(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,TBExt80 *result,uint16_t *flags);
static int vg_store_real(unsigned width,TBExt80 value,uint16_t control,uint64_t *bits,uint16_t *flags);
#define tb_bo_arithmetic vg_arithmetic
#define tb_bo_store_real vg_store_real
#define x87_exact_arithmetic vg_exact_arithmetic
#include "../llasm-support/tribes_float80.c"
#undef tb_bo_arithmetic
#undef tb_bo_store_real
#undef x87_exact_arithmetic

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
static int as_double(TBExt80 v,double *out){uint64_t bits;if(!as_double_bits(v,&bits))return 0;memcpy(out,&bits,8);return 1;}
static int double_fast(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,TBExt80 *result,uint16_t *flags){
 if(op>4||(control&0x0f00)!=0x0200||!x87_exact_host_nearest())return 0;
 double x,y=0,z,err,direction;if(!as_double(a,&x)||(op!=4&&!as_double(b,&y)))return 0;
 switch(op){
 case 0:case 1:{if(op==1)y=-y;z=x+y;double v=z-x;err=(x-(z-v))+(y-v);direction=err;break;}
 case 2:z=x*y;err=fma(x,y,-z);direction=err;break;
 case 3:if(y==0||(x!=0&&!(fabs(x)>=0x1p-960)))return 0;z=x/y;err=fma(-z,y,x);direction=y<0?-err:err;break;
 default:if(!(x>=0x1p-960))return 0;z=sqrt(x);err=fma(-z,z,x);direction=err;break;
 }
 uint64_t bits;memcpy(&bits,&z,8);
 if(z==0){if(err!=0||(op==2&&x!=0&&y!=0)||(op==3&&x!=0))return 0; /* an underflow, not a true zero */
  result->significand=0;result->sign_exponent=(uint16_t)((bits>>63)<<15);*flags=0;return 1;}
 if(!(fabs(z)>=0x1p-960)||!(fabs(z)<=0x1.fffffffffffffp1023))return 0;
 result->significand=UINT64_C(1)<<63|(bits&UINT64_C(0xfffffffffffff))<<11;
 result->sign_exponent=(uint16_t)((bits>>63)<<15|(((bits>>52)&0x7ff)-1023+16383));
 *flags=err==0?0:(uint16_t)(0x20|((direction<0)==(z>0)?0x200:0));return 1;
}
static int vg_arithmetic(unsigned op,TBExt80 a,TBExt80 b,uint16_t control,TBExt80 *result,uint16_t *flags){
 return single_fast(op,a,b,control,result,flags)||double_fast(op,a,b,control,result,flags)||tb_bo_arithmetic(op,a,b,control,result,flags);
}
static int vg_store_real(unsigned width,TBExt80 value,uint16_t control,uint64_t *bits,uint16_t *flags){
 float s;
 if(width==32&&as_float(value,&s)){uint32_t b;memcpy(&b,&s,4);*bits=b;*flags=0;return 1;}
 if(width==64&&as_double_bits(value,bits)){*flags=0;return 1;}
 double d;
 if(width==32&&!(control&0x0c00)&&as_double(value,&d)&&x87_exact_host_nearest()){
  float f=(float)d;   /* one rounding to nearest, as the x87 store makes it */
  if(f==0&&d!=0)return tb_bo_store_real(width,value,control,bits,flags);
  if(f!=0&&(!(fabsf(f)>=0x1p-126f)||!(fabsf(f)<=0x1.fffffep127f)))return tb_bo_store_real(width,value,control,bits,flags);
  double back=(double)f;uint32_t b;memcpy(&b,&f,4);*bits=b;
  *flags=back==d?0:(uint16_t)(0x20|(fabs(back)>fabs(d)?0x200:0));return 1;
 }
 return tb_bo_store_real(width,value,control,bits,flags);
}
#endif
