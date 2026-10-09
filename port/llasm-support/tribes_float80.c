/* Native extended80 x87 runtime under construction. Pure math is AOT-compiled;
   transcendental exception status and fault handling need further validation. */
#include "llasm_cpu.h"
#include "../core/ext80.h"
#include "../core/bochs_math.h"
#define LITTLEENDIAN 1
#include "softfloat.h"
#include <stdlib.h>
#include <string.h>

/* The checked pass proves only bounded fields of the 440-byte CPU object.
   Addresses loaded from its fields and guest output buffers remain checked. */
#define NATIVE_CPU __attribute__((annotate("tribes.native-cpu")))

EXTERNC uint32_t halopad_x87_result(_cpu *, const int64_t *);
static const TBExt80 indefinite={UINT64_C(0xc000000000000000),0xffff};
static NATIVE_CPU unsigned slot(CPU,unsigned n){if(n>=8||cpu->_st_top>=8||!cpu->_st_extended)abort();return (cpu->_st_top+n)&7;}
static unsigned tag(TBExt80 v){unsigned e=v.sign_exponent&0x7fff;if(!e&&!v.significand)return 1;if(!e||e==0x7fff||!(v.significand>>63))return 2;return 0;}
static NATIVE_CPU void exception(CPU,unsigned flags){cpu->_st_sw_cond|=flags;if(flags&(~cpu->_st_cw)&63){cpu->_st_sw_cond|=0x8080;abort();}}
static NATIVE_CPU TBExt80 read_st(CPU,unsigned n){unsigned i=slot(cpu,n);TBExt80 v;memcpy(&v.significand,&cpu->_st[i],8);v.sign_exponent=cpu->_st_exp[i];if(((cpu->_st_tag>>(2*i))&3)==3){cpu->_st_sw_cond&=~0x200u;exception(cpu,0x41);return indefinite;}return v;}
static NATIVE_CPU void write_st(CPU,unsigned n,TBExt80 v){unsigned i=slot(cpu,n);memcpy(&cpu->_st[i],&v.significand,8);cpu->_st_exp[i]=v.sign_exponent;cpu->_st_tag=(cpu->_st_tag&~(3u<<(2*i)))|(tag(v)<<(2*i));}
static NATIVE_CPU void pop(CPU){cpu->_st_tag|=3u<<(2*slot(cpu,0));cpu->_st_top=(cpu->_st_top+1)&7;}
static NATIVE_CPU void push(CPU,TBExt80 v){unsigned i=slot(cpu,7);if(((cpu->_st_tag>>(2*i))&3)!=3){cpu->_st_sw_cond|=0x200;exception(cpu,0x41);v=indefinite;}else cpu->_st_sw_cond&=~0x200u;cpu->_st_top=i;write_st(cpu,0,v);}
static extFloat80_t sf(TBExt80 v){extFloat80_t x={.signif=v.significand,.signExp=v.sign_exponent};return x;}
static TBExt80 tb(extFloat80_t x){return (TBExt80){x.signif,x.signExp};}
typedef struct {uint_fast8_t rounding,flags,precision,tininess;} Environment;
static NATIVE_CPU Environment begin(CPU){Environment e={softfloat_roundingMode,softfloat_exceptionFlags,extF80_roundingPrecision,softfloat_detectTininess};softfloat_roundingMode=(uint_fast8_t[]){0,2,3,1}[(cpu->_st_cw>>10)&3];softfloat_exceptionFlags=0;softfloat_detectTininess=softfloat_tininess_afterRounding;return e;}
static unsigned finish(Environment e){unsigned f=softfloat_exceptionFlags;softfloat_roundingMode=e.rounding;softfloat_exceptionFlags=e.flags;extF80_roundingPrecision=e.precision;softfloat_detectTininess=e.tininess;return ((f&16)?1:0)|((f&8)?4:0)|((f&4)?8:0)|((f&2)?16:0)|((f&1)?32:0);}
static NATIVE_CPU TBExt80 load32(CPU,uint32_t bits){Environment e=begin(cpu);TBExt80 v=tb(f32_to_extF80((float32_t){bits}));unsigned f=finish(e);if(!(bits&0x7f800000)&&(bits&0x7fffff))f|=2;exception(cpu,f);return v;}
static NATIVE_CPU TBExt80 load64(CPU,uint32_t lo,uint32_t hi){uint64_t bits=lo|((uint64_t)hi<<32);Environment e=begin(cpu);TBExt80 v=tb(f64_to_extF80((float64_t){bits}));unsigned f=finish(e);if(!(bits&UINT64_C(0x7ff0000000000000))&&(bits&UINT64_C(0xfffffffffffff)))f|=2;exception(cpu,f);return v;}
static TBExt80 integer(int64_t i){return tb(i64_to_extF80(i));}
static NATIVE_CPU void arithmetic(CPU,unsigned op,unsigned dst,TBExt80 a,TBExt80 b){TBExt80 z;uint16_t f;if(!tb_bo_arithmetic(op,a,b,cpu->_st_cw,&z,&f))abort();cpu->_st_sw_cond&=~0x200u;exception(cpu,f);write_st(cpu,dst,z);}
NATIVE_CPU void x87_fninit_void(CPU){cpu->_st_top=0;cpu->_st_sw_cond=0;cpu->_st_cw=0x37f;cpu->_st_tag=0xffff;cpu->_st_mmx_active=0;cpu->_st_extended=1;}
NATIVE_CPU void x87_fldcw_uint16(CPU,uint32_t cw){cpu->_st_cw=cw&0xffff;}
NATIVE_CPU uint32_t x87_fnstcw_void(CPU){return cpu->_st_cw&0xffff;}
NATIVE_CPU uint32_t x87_fnstsw_void(CPU){return (cpu->_st_sw_cond&~0x3800u)|(cpu->_st_top<<11);}
NATIVE_CPU void x87_fld_float(CPU,uint32_t x){push(cpu,load32(cpu,x));}
NATIVE_CPU void x87_fld_double(CPU,uint32_t lo,uint32_t hi){push(cpu,load64(cpu,lo,hi));}
NATIVE_CPU void x87_fild_int32(CPU,int32_t i){push(cpu,integer(i));}
NATIVE_CPU void x87_fild_int64(CPU,uint32_t lo,uint32_t hi){uint64_t bits=lo|((uint64_t)hi<<32);int64_t i;memcpy(&i,&bits,8);push(cpu,integer(i));}
NATIVE_CPU void x87_fld_st(CPU,int n){TBExt80 v=read_st(cpu,n);push(cpu,v);}
NATIVE_CPU void x87_fldz_void(CPU){push(cpu,(TBExt80){0,0});}
NATIVE_CPU void x87_fld1_void(CPU){push(cpu,(TBExt80){UINT64_C(0x8000000000000000),0x3fff});}
static NATIVE_CPU void constant(CPU,uint64_t m,uint16_t e){unsigned rc=(cpu->_st_cw>>10)&3;if(rc==1||rc==3)m--;push(cpu,(TBExt80){m,e});}
NATIVE_CPU void x87_fldlg2_void(CPU){constant(cpu,UINT64_C(0x9a209a84fbcff799),0x3ffd);}
NATIVE_CPU void x87_fldln2_void(CPU){constant(cpu,UINT64_C(0xb17217f7d1cf79ac),0x3ffe);}
NATIVE_CPU void x87_fldpi_void(CPU){constant(cpu,UINT64_C(0xc90fdaa22168c235),0x4000);}
NATIVE_CPU void x87_fabs_void(CPU){TBExt80 v=read_st(cpu,0);v.sign_exponent&=0x7fff;write_st(cpu,0,v);cpu->_st_sw_cond&=~0x200u;}
NATIVE_CPU void x87_fchs_void(CPU){TBExt80 v=read_st(cpu,0);v.sign_exponent^=0x8000;write_st(cpu,0,v);cpu->_st_sw_cond&=~0x200u;}
NATIVE_CPU void x87_fst_st(CPU,int n){write_st(cpu,n,read_st(cpu,0));cpu->_st_sw_cond&=~0x200u;}
NATIVE_CPU void x87_fstp_st(CPU,int n){x87_fst_st(cpu,n);pop(cpu);}
NATIVE_CPU void x87_fxch_st(CPU,int n){TBExt80 a=read_st(cpu,0),b=read_st(cpu,n);write_st(cpu,0,b);write_st(cpu,n,a);cpu->_st_sw_cond&=~0x200u;}
#define BINARY(prefix,op) \
NATIVE_CPU void x87_##prefix##_float(CPU,uint32_t x){TBExt80 b=load32(cpu,x);arithmetic(cpu,op,0,read_st(cpu,0),b);} \
NATIVE_CPU void x87_##prefix##_double(CPU,uint32_t lo,uint32_t hi){TBExt80 b=load64(cpu,lo,hi);arithmetic(cpu,op,0,read_st(cpu,0),b);} \
NATIVE_CPU void x87_##prefix##_st(CPU,int n){arithmetic(cpu,op,0,read_st(cpu,0),read_st(cpu,n));} \
NATIVE_CPU void x87_##prefix##_to_st(CPU,int n){arithmetic(cpu,op,n,read_st(cpu,n),read_st(cpu,0));} \
NATIVE_CPU void x87_##prefix##p_st(CPU,int n){x87_##prefix##_to_st(cpu,n);pop(cpu);}
BINARY(fadd,TB_EXT_ADD)
BINARY(fsub,TB_EXT_SUB)
BINARY(fmul,TB_EXT_MUL)
BINARY(fdiv,TB_EXT_DIV)
#define REVERSE(prefix,op) \
NATIVE_CPU void x87_##prefix##r_float(CPU,uint32_t x){TBExt80 a=load32(cpu,x);arithmetic(cpu,op,0,a,read_st(cpu,0));} \
NATIVE_CPU void x87_##prefix##r_double(CPU,uint32_t lo,uint32_t hi){TBExt80 a=load64(cpu,lo,hi);arithmetic(cpu,op,0,a,read_st(cpu,0));} \
NATIVE_CPU void x87_##prefix##r_st(CPU,int n){arithmetic(cpu,op,0,read_st(cpu,n),read_st(cpu,0));} \
NATIVE_CPU void x87_##prefix##r_to_st(CPU,int n){arithmetic(cpu,op,n,read_st(cpu,0),read_st(cpu,n));} \
NATIVE_CPU void x87_##prefix##rp_st(CPU,int n){x87_##prefix##r_to_st(cpu,n);pop(cpu);}
REVERSE(fsub,TB_EXT_SUB)
REVERSE(fdiv,TB_EXT_DIV)
#define INTOP(name,op,reverse) NATIVE_CPU void x87_##name##_int32(CPU,int32_t n){TBExt80 a=read_st(cpu,0),b=integer(n);arithmetic(cpu,op,0,reverse?b:a,reverse?a:b);}
INTOP(fiadd,TB_EXT_ADD,0)
INTOP(fisub,TB_EXT_SUB,0)
INTOP(fisubr,TB_EXT_SUB,1)
INTOP(fimul,TB_EXT_MUL,0)
INTOP(fidiv,TB_EXT_DIV,0)
INTOP(fidivr,TB_EXT_DIV,1)
NATIVE_CPU void x87_fsqrt_void(CPU){arithmetic(cpu,TB_EXT_SQRT,0,read_st(cpu,0),indefinite);}
/* Memory stores define C1 from this conversion, not the preceding arithmetic. */
static NATIVE_CPU uint64_t store_real(CPU,unsigned width){TBExt80 v=read_st(cpu,0);uint64_t z;uint16_t f;if(!tb_bo_store_real(width,v,cpu->_st_cw,&z,&f))abort();cpu->_st_sw_cond&=~0x200u;exception(cpu,f);return z;}
NATIVE_CPU int32_t x87_fst_float(CPU){return (int32_t)store_real(cpu,32);}
NATIVE_CPU int32_t x87_fstp_float(CPU){int32_t z=x87_fst_float(cpu);pop(cpu);return z;}
NATIVE_CPU uint32_t x87_fst_double(CPU){uint64_t z=store_real(cpu,64);memcpy(&cpu->_st_result,&z,8);return halopad_x87_result(cpu,&cpu->_st_result);}
NATIVE_CPU uint32_t x87_fstp_double(CPU){uint32_t a=x87_fst_double(cpu);pop(cpu);return a;}
static NATIVE_CPU int64_t store_integer(CPU,unsigned width){TBExt80 v=read_st(cpu,0);int64_t z;uint16_t f;if(!tb_bo_store_integer(width,v,cpu->_st_cw,&z,&f))abort();cpu->_st_sw_cond&=~0x200u;exception(cpu,f);return z;}
NATIVE_CPU int32_t x87_fist_int32(CPU){return (int32_t)store_integer(cpu,32);}
NATIVE_CPU int16_t x87_fistp_int16(CPU){int16_t z=(int16_t)store_integer(cpu,16);pop(cpu);return z;}
NATIVE_CPU int32_t x87_fistp_int32(CPU){int32_t z=x87_fist_int32(cpu);pop(cpu);return z;}
NATIVE_CPU uint32_t x87_fistp_int64(CPU){cpu->_st_result=store_integer(cpu,64);pop(cpu);return halopad_x87_result(cpu,&cpu->_st_result);}
NATIVE_CPU void x87_frndint_void(CPU){arithmetic(cpu,TB_BO_ROUND_INTEGER,0,read_st(cpu,0),indefinite);}
NATIVE_CPU void x87_fld_tword(CPU,uint32_t a){unsigned char *p=REG2PTR(a);TBExt80 v={0,0};for(unsigned i=0;i<8;i++)v.significand|=(uint64_t)p[i]<<(8*i);v.sign_exponent=p[8]|((uint16_t)p[9]<<8);push(cpu,v);}
NATIVE_CPU void x87_fstp_tword(CPU,uint32_t a){TBExt80 v=read_st(cpu,0);unsigned char *p=REG2PTR(a);for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(v.significand>>(8*i));p[8]=v.sign_exponent;p[9]=v.sign_exponent>>8;pop(cpu);}
NATIVE_CPU void tb_x87_fbld(CPU,uint32_t a){unsigned char *p=REG2PTR(a);uint64_t n=0;for(int i=8;i>=0;i--)n=n*100+(p[i]>>4)*10+(p[i]&15);TBExt80 v=tb(ui64_to_extF80(n));if(p[9]&128)v.sign_exponent|=0x8000;push(cpu,v);}
NATIVE_CPU void tb_x87_fbstp(CPU,uint32_t a){TBExt80 v=read_st(cpu,0);Environment e=begin(cpu);int invalid=(v.sign_exponent&0x7fff)&&!(v.significand>>63);int64_t n=invalid?0:extF80_to_i64(sf(v),softfloat_roundingMode,true);unsigned f=finish(e)|(invalid?1:0);unsigned char bytes[10]={0};cpu->_st_sw_cond&=~0x200u;if((f&1)||n>INT64_C(999999999999999999)||n< -INT64_C(999999999999999999)){f=1;bytes[7]=0xc0;bytes[8]=bytes[9]=0xff;}else{uint64_t m=n<0?(uint64_t)(-n):(uint64_t)n;for(unsigned i=0;i<9;i++){unsigned lo=m%10;m/=10;unsigned hi=m%10;m/=10;bytes[i]=lo|(hi<<4);}bytes[9]=(v.sign_exponent>>8)&128;TBExt80 magnitude=v;magnitude.sign_exponent&=0x7fff;TBExt80 rounded=tb(ui64_to_extF80(n<0?(uint64_t)(-n):(uint64_t)n));if((f&32)&&extF80_lt(sf(magnitude),sf(rounded)))cpu->_st_sw_cond|=0x200;}exception(cpu,f);memcpy(REG2PTR(a),bytes,10);pop(cpu);}
NATIVE_CPU void tb_x87_fxtract(CPU){TBExt80 v=read_st(cpu,0),exponent;unsigned e=v.sign_exponent&0x7fff;if(!v.significand&&!e){exception(cpu,4);exponent=(TBExt80){UINT64_C(0x8000000000000000),0xffff};}else if(e&&!(v.significand>>63)){exception(cpu,1);v=exponent=indefinite;}else if(e==0x7fff){if(v.significand==UINT64_C(0x8000000000000000))exponent=(TBExt80){v.significand,0x7fff};else {if(!(v.significand&UINT64_C(0x4000000000000000)))exception(cpu,1);v.significand|=UINT64_C(0x4000000000000000);exponent=v;}}else {int unbiased=(e?e:1)-16383;if(!e)exception(cpu,2);if(!e&&!(v.significand>>63)){unsigned shift=__builtin_clzll(v.significand);v.significand<<=shift;unbiased-=shift;}exponent=integer(unbiased);v.sign_exponent=(v.sign_exponent&0x8000)|0x3fff;}write_st(cpu,0,exponent);push(cpu,v);}
/* Compare raw canonical values without narrowing either significand. */
static NATIVE_CPU void compare(CPU,TBExt80 a,TBExt80 b,int quiet){
 unsigned ae=a.sign_exponent&0x7fff,be=b.sign_exponent&0x7fff;
 int bad=(ae&&!(a.significand>>63))||(be&&!(b.significand>>63));
 int an=ae==0x7fff&&a.significand!=UINT64_C(0x8000000000000000);
 int bn=be==0x7fff&&b.significand!=UINT64_C(0x8000000000000000);
 unsigned result=0,flags=0;
 if(bad||an||bn){result=0x4500;if(bad||!quiet||(an&&!(a.significand>>62&1))||(bn&&!(b.significand>>62&1)))flags=1;}
 else {
  if(!ae&&a.significand)flags|=2;
  if(!be&&b.significand)flags|=2;
  if(!ae&&(a.significand>>63))ae=1;
  if(!be&&(b.significand>>63))be=1;
  int az=!ae&&!a.significand,bz=!be&&!b.significand;
  if((az&&bz)||(a.sign_exponent==b.sign_exponent&&a.significand==b.significand)||(ae==be&&a.significand==b.significand&&((a.sign_exponent^b.sign_exponent)&0x8000)==0))result=0x4000;
  else {
   int less;
   if((a.sign_exponent^b.sign_exponent)&0x8000)less=(a.sign_exponent>>15)!=0;
   else {less=ae<be||(ae==be&&a.significand<b.significand);if(a.sign_exponent&0x8000)less=!less;}
   if(less)result=0x100;
  }
 }
 exception(cpu,flags);
 cpu->_st_sw_cond=(cpu->_st_sw_cond&~0x4700u)|result;
}
NATIVE_CPU void x87_fcom_st(CPU,int n){compare(cpu,read_st(cpu,0),read_st(cpu,n),0);}
NATIVE_CPU void x87_fcomp_st(CPU,int n){x87_fcom_st(cpu,n);pop(cpu);}
NATIVE_CPU void x87_fcompp_void(CPU){x87_fcom_st(cpu,1);pop(cpu);pop(cpu);}
NATIVE_CPU void x87_fucom_st(CPU,int n){compare(cpu,read_st(cpu,0),read_st(cpu,n),1);}
NATIVE_CPU void x87_fucomp_st(CPU,int n){x87_fucom_st(cpu,n);pop(cpu);}
NATIVE_CPU void x87_fucompp_void(CPU){x87_fucom_st(cpu,1);pop(cpu);pop(cpu);}
NATIVE_CPU void x87_fcom_float(CPU,uint32_t bits){TBExt80 b=load32(cpu,bits);compare(cpu,read_st(cpu,0),b,0);}
NATIVE_CPU void x87_fcomp_float(CPU,uint32_t bits){x87_fcom_float(cpu,bits);pop(cpu);}
NATIVE_CPU void x87_fcom_double(CPU,uint32_t lo,uint32_t hi){TBExt80 b=load64(cpu,lo,hi);compare(cpu,read_st(cpu,0),b,0);}
NATIVE_CPU void x87_fcomp_double(CPU,uint32_t lo,uint32_t hi){x87_fcom_double(cpu,lo,hi);pop(cpu);}

static NATIVE_CPU int software_math(CPU,unsigned op,TBExt80 *result,uint64_t *quotient) {
 TBExt80 a=read_st(cpu,0),b=op>=4?read_st(cpu,1):indefinite;
 uint16_t flags;int rc=tb_bo_math(op,a,b,cpu->_st_cw,result,&flags,quotient);
 cpu->_st_sw_cond&=~0x200u;
 exception(cpu,flags);
 return rc;
}
NATIVE_CPU void x87_f2xm1_void(CPU){TBExt80 z;uint64_t q;software_math(cpu,0,&z,&q);write_st(cpu,0,z);}
static NATIVE_CPU void trig(CPU,unsigned op){TBExt80 z;uint64_t q;int rc=software_math(cpu,op,&z,&q);if(rc==-1){cpu->_st_sw_cond|=0x400;return;}cpu->_st_sw_cond&=~0x400u;write_st(cpu,0,z);}
NATIVE_CPU void x87_fsin_void(CPU){trig(cpu,1);}
NATIVE_CPU void x87_fcos_void(CPU){trig(cpu,2);}
NATIVE_CPU void x87_fptan_void(CPU){TBExt80 z;uint64_t q;int rc=software_math(cpu,3,&z,&q);if(rc==-1){cpu->_st_sw_cond|=0x400;return;}cpu->_st_sw_cond&=~0x400u;write_st(cpu,0,z);unsigned e=z.sign_exponent&0x7fff;unsigned c1=cpu->_st_sw_cond&0x200;int full=((cpu->_st_tag>>(2*slot(cpu,7)))&3)!=3;push(cpu,e==0x7fff&&z.significand!=UINT64_C(0x8000000000000000)?z:(TBExt80){UINT64_C(0x8000000000000000),0x3fff});if(!full)cpu->_st_sw_cond|=c1;}
NATIVE_CPU void x87_fyl2x_void(CPU){TBExt80 z;uint64_t q;software_math(cpu,4,&z,&q);write_st(cpu,1,z);pop(cpu);}
NATIVE_CPU void x87_fpatan_void(CPU){TBExt80 z;uint64_t q;software_math(cpu,5,&z,&q);write_st(cpu,1,z);pop(cpu);}
NATIVE_CPU void x87_fprem_void(CPU){TBExt80 z;uint64_t q;int rc=software_math(cpu,6,&z,&q);write_st(cpu,0,z);if(rc==1)cpu->_st_sw_cond|=0x400;else if(rc==0)cpu->_st_sw_cond=(cpu->_st_sw_cond&~0x4700u)|((q&4)<<6)|((q&2)<<13)|((q&1)<<9);}
NATIVE_CPU void x87_fscale_void(CPU){TBExt80 z;uint64_t q;software_math(cpu,7,&z,&q);write_st(cpu,0,z);}
