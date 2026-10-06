#include "llasm_cpu.h"
#include "tribes_sse.h"
#include "../core/sse_arithmetic.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *reg(CPU,unsigned index) {
    if(!cpu||index>=8)abort();
    return cpu->_xmm[index];
}
static unsigned width(unsigned mode) {
    switch(mode) {
    case TB_SSE_SS:case TB_SSE_D:return 4;
    case TB_SSE_LPD:case TB_SSE_Q:case TB_SSE_SD:return 8;
    case TB_SSE_U128:case TB_SSE_A128:return 16;
    default:abort();
    }
}
static unsigned char *memory(CPU,uint32_t address,unsigned mode) {
    unsigned n=width(mode);
    if(!cpu||(mode==TB_SSE_A128&&(address&15))||(uint64_t)address+n>UINT64_C(0x100000000))abort();
    uintptr_t base=(uintptr_t)cpu->_pointer_offset;
    if(base>UINTPTR_MAX-(uint64_t)address-n)abort();
    return (unsigned char *)(base+address);
}
void tb_sse_init(CPU) {
    if(!cpu)abort();
    memset(cpu->_xmm,0,sizeof cpu->_xmm);
    memset(cpu->_ymm_hi,0,sizeof cpu->_ymm_hi);cpu->_mxcsr=0x1f80;
}
/* Original math prologue STMXCSR [ESP+4]. Save tracked guest state; reserved
   high bits store as zero, and neither CPU state nor native FP state changes. */
void tb_math_stmxcsr_1010cef3(CPU) {
    uint32_t value=cpu->_mxcsr&UINT32_C(0xffff);
    memcpy(memory(cpu,cpu->_esp+4,TB_SSE_D),&value,4);
}
void tb_avx_ssreg(CPU,unsigned dest,unsigned merge,unsigned source) {
    unsigned char *dst=reg(cpu,dest),value[16],scalar[4];
    /* Snapshot all operands before publication: any of the three roles may
       alias. Both VEX register opcodes use this same architectural mapping. */
    memcpy(value,reg(cpu,merge),16);memcpy(scalar,reg(cpu,source),4);
    memcpy(value,scalar,4);memcpy(dst,value,16);
    memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_ssload(CPU,unsigned dest,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[4];
    memcpy(value,memory(cpu,address,TB_SSE_SS),4);
    memcpy(dst,value,4);memset(dst+4,0,12);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_ssstore(CPU,unsigned source,uint32_t address) {
    unsigned char value[4];memcpy(value,reg(cpu,source),4);
    /* The memory-destination form changes no vector register, including the
       source's upper YMM half. Alignment follows the scalar memory contract. */
    memcpy(memory(cpu,address,TB_SSE_SS),value,4);
}
void tb_sse_load(CPU,unsigned index,uint32_t address,unsigned mode) {
    unsigned char *dst=reg(cpu,index);unsigned n=width(mode);
    unsigned char value[16];memcpy(value,memory(cpu,address,mode),n);
    /* MOVSS/MOVSD memory and MOVD/MOVQ clear the remaining XMM lanes. MOVLPD
       retains the upper quadword; all legacy forms retain future YMM lanes. */
    memcpy(dst,value,n);
    if(mode==TB_SSE_SS||mode==TB_SSE_D||mode==TB_SSE_Q||mode==TB_SSE_SD)memset(dst+n,0,16-n);
}
void tb_sse_store(CPU,unsigned index,uint32_t address,unsigned mode) {
    unsigned char value[16];unsigned n=width(mode);memcpy(value,reg(cpu,index),n);
    memcpy(memory(cpu,address,mode),value,n);
}
void tb_sse_regmove(CPU,unsigned dest,unsigned source,unsigned mode) {
    unsigned char *dst=reg(cpu,dest),value[16];unsigned n=width(mode);
    /* MOVLPD and MOVD have no XMM-to-XMM form. Reject decoder misuse. */
    if(mode==TB_SSE_LPD||mode==TB_SSE_D)abort();
    memcpy(value,reg(cpu,source),n);memcpy(dst,value,n);
    if(mode==TB_SSE_Q)memset(dst+8,0,8);
}
void tb_sse_setd(CPU,unsigned index,uint32_t value) {
    unsigned char *dst=reg(cpu,index);
    for(unsigned i=0;i<4;i++)dst[i]=(unsigned char)(value>>(8*i));
    memset(dst+4,0,12);
}
uint32_t tb_sse_getd(CPU,unsigned index) {
    const unsigned char *src=reg(cpu,index);uint32_t value=0;
    for(unsigned i=0;i<4;i++)value|=(uint32_t)src[i]<<(8*i);
    return value;
}
static void binary(unsigned char *dst,const unsigned char source[16],unsigned op) {
    if(op>TB_SSE_AND)abort();
    if(op==TB_SSE_EQW) {
        for(unsigned i=0;i<16;i+=2) {
            unsigned char equal=(dst[i]==source[i]&&dst[i+1]==source[i+1])?255:0;
            dst[i]=equal;dst[i+1]=equal;
        }
    } else for(unsigned i=0;i<16;i++)dst[i]=op==TB_SSE_XOR?(dst[i]^source[i]):op==TB_SSE_AND?(dst[i]&source[i]):(dst[i]|source[i]);
}
void tb_sse_binary(CPU,unsigned op,unsigned dest,unsigned source) {
    unsigned char value[16];unsigned char *dst=reg(cpu,dest);if(op>TB_SSE_AND)abort();
    memcpy(value,reg(cpu,source),16);binary(dst,value,op);
}
void tb_sse_binarymem(CPU,unsigned op,unsigned dest,uint32_t address) {
    unsigned char value[16];unsigned char *dst=reg(cpu,dest);if(op>TB_SSE_AND)abort();
    /* Legacy SSE computation memory operands require 16-byte guest alignment. */
    memcpy(value,memory(cpu,address,TB_SSE_A128),16);binary(dst,value,op);
}
uint32_t tb_sse_mask(CPU,unsigned index) {
    const unsigned char *src=reg(cpu,index);uint32_t value=0;
    for(unsigned i=0;i<16;i++)value|=(uint32_t)(src[i]>>7)<<i;
    return value;
}
static void shuffle(unsigned char *dst,const unsigned char source[16],unsigned op,unsigned control) {
    if(op>TB_SSE_SHUFLW||control>255)abort();
    unsigned n=op==TB_SSE_SHUFD?4:2;
    for(unsigned i=0;i<4;i++)memcpy(dst+i*n,source+((control>>(2*i))&3)*n,n);
    if(op==TB_SSE_SHUFLW)memcpy(dst+8,source+8,8);
}
void tb_sse_shuffle(CPU,unsigned op,unsigned dest,unsigned source,unsigned control) {
    unsigned char value[16];unsigned char *dst=reg(cpu,dest);
    if(op>TB_SSE_SHUFLW||control>255)abort();
    memcpy(value,reg(cpu,source),16);shuffle(dst,value,op,control);
}
void tb_sse_shufflemem(CPU,unsigned op,unsigned dest,uint32_t address,unsigned control) {
    unsigned char value[16];unsigned char *dst=reg(cpu,dest);
    if(op>TB_SSE_SHUFLW||control>255)abort();
    memcpy(value,memory(cpu,address,TB_SSE_A128),16);shuffle(dst,value,op,control);
}
static uint64_t signed32_double(uint32_t value) {
    unsigned sign=value>>31;uint32_t magnitude=sign?0u-value:value;
    if(!magnitude)return 0;
    unsigned top=31u-(unsigned)__builtin_clz(magnitude);
    uint64_t fraction=((uint64_t)magnitude<<(52-top))&UINT64_C(0x000fffffffffffff);
    return ((uint64_t)sign<<63)|((uint64_t)(1023+top)<<52)|fraction;
}
static void dq2pd(unsigned char *dst,const unsigned char source[8]) {
    for(unsigned lane=0;lane<2;lane++) {
        uint32_t input=0;for(unsigned i=0;i<4;i++)input|=(uint32_t)source[4*lane+i]<<(8*i);
        /* Every signed 32-bit integer is exact in binary64. No rounding mode
           or MXCSR exception flag is involved, including INT32_MIN. */
        uint64_t output=signed32_double(input);
        for(unsigned i=0;i<8;i++)dst[8*lane+i]=(unsigned char)(output>>(8*i));
    }
}
void tb_sse_dq2pd(CPU,unsigned dest,unsigned source) {
    unsigned char value[8];unsigned char *dst=reg(cpu,dest);
    memcpy(value,reg(cpu,source),8);dq2pd(dst,value);
}
void tb_sse_dq2pdmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[8];unsigned char *dst=reg(cpu,dest);
    memcpy(value,memory(cpu,address,TB_SSE_Q),8);dq2pd(dst,value);
}
static uint64_t read64(const unsigned char *src) {
    uint64_t value=0;
    for(unsigned i=0;i<8;i++)value|=(uint64_t)src[i]<<(8*i);
    return value;
}
static void ucomisd(CPU,uint64_t left,uint64_t right) {
    const uint64_t sign=UINT64_C(0x8000000000000000);
    uint64_t a=left&~sign,b=right&~sign;
    int nan_a=a>UINT64_C(0x7ff0000000000000);
    int nan_b=b>UINT64_C(0x7ff0000000000000);
    unsigned raised=0,condition;
    /* Intel's precomputation priority suppresses the denormal exception when
       a NaN produces the unordered result. QNaN alone does not raise IE. */
    if(nan_a||nan_b) {
        if((nan_a&&!(a&UINT64_C(0x0008000000000000))) ||
           (nan_b&&!(b&UINT64_C(0x0008000000000000))))raised=1;
        condition=ZF|PF|CF;
    } else {
        int den_a=a&&a<UINT64_C(0x0010000000000000);
        int den_b=b&&b<UINT64_C(0x0010000000000000);
        if(cpu->_mxcsr&0x40) {
            if(den_a){left&=sign;a=0;}
            if(den_b){right&=sign;b=0;}
        } else if(den_a||den_b)raised=2;
        if(left==right||(!a&&!b))condition=ZF;
        else {
            uint64_t key_a=(left&sign)?~left:(left^sign);
            uint64_t key_b=(right&sign)?~right:(right^sign);
            condition=key_a<key_b?CF:0;
        }
    }
    cpu->_mxcsr|=raised;
    /* An unmasked fault must not publish flags. Native Windows SEH delivery
       is unfinished, so stop at that boundary instead of returning success. */
    if(raised&~(cpu->_mxcsr>>7))abort();
    cpu->_eflags=(cpu->_eflags&~(OF|SF|AF|ZF|PF|CF))|condition;
}
void tb_sse_ucomisd(CPU,unsigned left,unsigned right) {
    uint64_t a=read64(reg(cpu,left)),b=read64(reg(cpu,right));
    ucomisd(cpu,a,b);
}
void tb_sse_ucomisdmem(CPU,unsigned left,uint32_t address) {
    uint64_t a=read64(reg(cpu,left)),b=read64(memory(cpu,address,TB_SSE_Q));
    ucomisd(cpu,a,b);
}
static uint32_t cvttss2si(CPU,uint32_t bits) {
    unsigned sign=bits>>31,exponent=(bits>>23)&255;
    uint32_t fraction=bits&0x7fffff,result=0;
    unsigned raised=0;
    if((bits&0x7fffffff)==0 || (!exponent&&(cpu->_mxcsr&0x40)))return 0;
    int power=(int)exponent-127;
    if(power>=31) {
        /* Exactly -2^31 is valid. All other binary32 values of this scale,
           infinities and either NaN class give masked integer indefinite. */
        result=0x80000000u;
        if(!(sign&&power==31&&!fraction))raised=1;
    } else if(power<0) {
        /* Integer conversions do not raise DE, including subnormal inputs.
           Nonzero fractional input loses precision unless DAZ made it zero. */
        raised=32;
    } else {
        uint32_t significand=0x800000u|fraction;
        if(power>=23)result=significand<<(power-23);
        else {
            unsigned shift=23-(unsigned)power;
            result=significand>>shift;
            if(significand&((1u<<shift)-1))raised=32;
        }
        if(sign)result=0u-result;
    }
    cpu->_mxcsr|=raised;
    /* Leave the destination untouched if the caller cannot service #XM. */
    if(raised&~(cpu->_mxcsr>>7))abort();
    return result;
}
uint32_t tb_sse_cvttss2si(CPU,unsigned source) {
    return cvttss2si(cpu,tb_sse_getd(cpu,source));
}
uint32_t tb_sse_cvttss2simem(CPU,uint32_t address) {
    const unsigned char *src=memory(cpu,address,TB_SSE_SS);uint32_t bits=0;
    for(unsigned i=0;i<4;i++)bits|=(uint32_t)src[i]<<(8*i);
    return cvttss2si(cpu,bits);
}
static uint64_t rounded_shift(uint64_t significand,unsigned shift,unsigned rounding,unsigned sign,unsigned *inexact) {
    uint64_t whole=shift<64?significand>>shift:0;
    uint64_t remainder=shift<64?significand&((UINT64_C(1)<<shift)-1):significand;
    *inexact=remainder!=0;
    if(remainder) {
        if(rounding==0&&shift<=64) {
            uint64_t half=UINT64_C(1)<<(shift-1);
            if(remainder>half||(remainder==half&&(whole&1)))whole++;
        } else if((rounding==1&&sign)||(rounding==2&&!sign))whole++;
    }
    return whole;
}
uint32_t tb_sse_cvtsd2si(CPU,unsigned source) {
    uint64_t bits=read64(reg(cpu,source)),fraction=bits&UINT64_C(0x000fffffffffffff);
    unsigned sign=(unsigned)(bits>>63),exponent=(unsigned)((bits>>52)&2047);
    uint32_t control=cpu->_mxcsr,result=0;unsigned raised=0;
    if(!(bits&UINT64_C(0x7fffffffffffffff))||(!exponent&&(control&0x40)))return 0;
    int power=(int)exponent-1023;
    if(power>31){result=0x80000000u;raised=1;}
    else {
        uint64_t significand=fraction|(exponent?UINT64_C(0x0010000000000000):0);
        unsigned inexact=0,shift=exponent?(unsigned)(52-power):1074;
        uint64_t whole=rounded_shift(significand,shift,(control>>13)&3,sign,&inexact);
        /* Range follows rounding, including negative fractions near -2^31.
           Invalid suppresses precision. Integer conversion never raises DE. */
        if(whole>(sign?UINT64_C(0x80000000):UINT64_C(0x7fffffff))) {
            result=0x80000000u;raised=1;
        } else {result=(uint32_t)whole;if(sign)result=0u-result;if(inexact)raised=32;}
    }
    cpu->_mxcsr|=raised;
    if(raised&~(control>>7))abort();
    return result;
}
static uint32_t double_single(uint64_t bits,uint32_t control,unsigned *raised) {
    unsigned sign=(unsigned)(bits>>63),exponent=(unsigned)((bits>>52)&2047);
    uint32_t output_sign=sign<<31;uint64_t fraction=bits&UINT64_C(0x000fffffffffffff);
    if(exponent==2047) {
        if(!fraction)return output_sign|0x7f800000u;
        if(!(fraction&UINT64_C(0x0008000000000000)))*raised|=1;
        return output_sign|0x7fc00000u|(uint32_t)(fraction>>29);
    }
    if(!exponent) {
        if(!fraction||(control&0x40))return output_sign;
        *raised|=2;
    }
    int power=exponent?(int)exponent-1023:-1022;
    uint64_t significand=fraction|(exponent?UINT64_C(0x0010000000000000):0);
    unsigned shift=power < -126 ? (unsigned)(-power-97):29;
    unsigned rounding=(control>>13)&3;
    unsigned inexact;
    uint64_t whole=rounded_shift(significand,shift,rounding,sign,&inexact);
    if(inexact)*raised|=32;
    if(power < -126) {
        /* Intel tests the working-precision result's unbounded exponent.
           A rounding carry at that precision can make it normal before the
           denormal/FTZ stage. Otherwise bounded rounding can produce minimum
           normal and still raise UE. Avoid conflating the two boundaries. */
        unsigned working_inexact;
        if(power==-127&&rounded_shift(significand,29,rounding,sign,&working_inexact)==UINT64_C(0x01000000))return output_sign|0x00800000u;
        if(control&0x8000) {*raised|=16|32;return output_sign;}
        if(inexact)*raised|=16;
        return output_sign|(uint32_t)whole;
    }
    int target_exponent=power+127;
    if(whole==UINT64_C(0x01000000)){whole>>=1;target_exponent++;}
    if(target_exponent>=255) {
        *raised|=8|32;
        int infinity=rounding==0||(rounding==1&&sign)||(rounding==2&&!sign);
        return output_sign|(infinity?0x7f800000u:0x7f7fffffu);
    }
    return output_sign|((uint32_t)target_exponent<<23)|((uint32_t)whole&0x7fffff);
}
static void pd2ps(CPU,unsigned char *dst,const unsigned char source[16]) {
    unsigned raised=0;uint32_t output[2];
    for(unsigned i=0;i<2;i++)output[i]=double_single(read64(source+8*i),cpu->_mxcsr,&raised);
    cpu->_mxcsr|=raised;
    /* Windows SIMD exception delivery and unmasked packed-lane priority
       remain unfinished. Do not publish destination lanes at that boundary. */
    if(raised&~(cpu->_mxcsr>>7))abort();
    for(unsigned lane=0;lane<2;lane++)for(unsigned i=0;i<4;i++)dst[4*lane+i]=(unsigned char)(output[lane]>>(8*i));
    memset(dst+8,0,8);
}
void tb_sse_pd2ps(CPU,unsigned dest,unsigned source) {
    unsigned char value[16];unsigned char *dst=reg(cpu,dest);
    memcpy(value,reg(cpu,source),16);pd2ps(cpu,dst,value);
}
void tb_sse_pd2psmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[16];unsigned char *dst=reg(cpu,dest);
    memcpy(value,memory(cpu,address,TB_SSE_A128),16);pd2ps(cpu,dst,value);
}
static void scalar(CPU,unsigned op,unsigned char *dst,const unsigned char *source) {
    unsigned n=op>=TB_SSE_MUL32?4:8;
    uint64_t a=0,b=0,result;uint32_t raised;
    for(unsigned i=0;i<n;i++){a|=(uint64_t)dst[i]<<(8*i);b|=(uint64_t)source[i]<<(8*i);}
    if(!tb_sse_arithmetic(op,a,b,cpu->_mxcsr,&result,&raised))abort();
    cpu->_mxcsr|=raised;
    /* Preserve destination and upper scalar lanes at an unserviced #XM.
       Precise unmasked exception delivery/priority is a separate contract. */
    if(raised&~(cpu->_mxcsr>>7))abort();
    for(unsigned i=0;i<n;i++)dst[i]=(unsigned char)(result>>(8*i));
}
void tb_sse_scalar(CPU,unsigned op,unsigned dest,unsigned source) {
    if(op>TB_SSE_MUL32&&op!=TB_SSE_ADD32&&op!=TB_SSE_DIV32&&op!=TB_SSE_SUB32)abort();
    unsigned n=op>=TB_SSE_MUL32?4:8;unsigned char value[8];
    unsigned char *dst=reg(cpu,dest);memcpy(value,reg(cpu,source),n);
    scalar(cpu,op,dst,value);
}
void tb_sse_scalarmem(CPU,unsigned op,unsigned dest,uint32_t address) {
    if(op>TB_SSE_MUL32&&op!=TB_SSE_ADD32&&op!=TB_SSE_DIV32&&op!=TB_SSE_SUB32)abort();
    unsigned n=op>=TB_SSE_MUL32?4:8;unsigned char value[8];
    unsigned char *dst=reg(cpu,dest);
    memcpy(value,memory(cpu,address,op>=TB_SSE_MUL32?TB_SSE_SS:TB_SSE_Q),n);
    scalar(cpu,op,dst,value);
}

void tb_avx_scalar(CPU,unsigned op,unsigned dest,unsigned merge,unsigned source) {
    if(op!=TB_SSE_MUL32 && op!=TB_SSE_ADD32 && op!=TB_SSE_DIV32 && op!=TB_SSE_SUB32)abort();
    unsigned char *dst=reg(cpu,dest),value[16],right[4];
    memcpy(value,reg(cpu,merge),16);memcpy(right,reg(cpu,source),4);
    /* Candidate lanes remain private until the masked arithmetic succeeds.
       An unserviced exception cannot expose merge lanes or clear YMM state. */
    scalar(cpu,op,value,right);memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_scalarmem(CPU,unsigned op,unsigned dest,unsigned merge,uint32_t address) {
    if(op!=TB_SSE_MUL32 && op!=TB_SSE_ADD32 && op!=TB_SSE_DIV32 && op!=TB_SSE_SUB32)abort();
    unsigned char *dst=reg(cpu,dest),value[16],right[4];
    memcpy(value,reg(cpu,merge),16);memcpy(right,memory(cpu,address,TB_SSE_SS),4);
    scalar(cpu,op,value,right);memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
static uint64_t single_double(uint32_t input,uint32_t control,unsigned *raised) {
    uint64_t sign=(uint64_t)(input>>31)<<63,result;
    unsigned exponent=(input>>23)&255,fraction=input&0x7fffff;*raised=0;
    if(exponent==255) {
        result=sign|UINT64_C(0x7ff0000000000000)|((uint64_t)fraction<<29);
        if(fraction) {
            if(!(fraction&0x400000))*raised=1;
            result|=UINT64_C(0x0008000000000000);
        }
    } else if(exponent) {
        result=sign|((uint64_t)(exponent+896)<<52)|((uint64_t)fraction<<29);
    } else if(!fraction || (control&0x40))result=sign;
    else {
        unsigned top=31u-(unsigned)__builtin_clz(fraction);*raised=2;
        result=sign|((uint64_t)(874+top)<<52)|(((uint64_t)fraction<<(52-top))&UINT64_C(0x000fffffffffffff));
    }
    /* Every finite binary32 input is exact and normal in binary64 (or zero).
       RC/FTZ do not affect the result; DAZ affects the source subnormal. */
    return result;
}
static void ss2sd(CPU,unsigned char value[16],const unsigned char source[4]) {
    uint32_t input;memcpy(&input,source,4);unsigned raised;
    uint64_t result=single_double(input,cpu->_mxcsr,&raised);
    cpu->_mxcsr|=raised;if(raised&~(cpu->_mxcsr>>7))abort();
    memcpy(value,&result,8);
}
void tb_avx_ss2sd(CPU,unsigned dest,unsigned merge,unsigned source) {
    unsigned char *dst=reg(cpu,dest),value[16],scalar[4];
    memcpy(value,reg(cpu,merge),16);memcpy(scalar,reg(cpu,source),4);
    ss2sd(cpu,value,scalar);memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_ss2sdmem(CPU,unsigned dest,unsigned merge,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[16],scalar[4];
    memcpy(value,reg(cpu,merge),16);memcpy(scalar,memory(cpu,address,TB_SSE_SS),4);
    ss2sd(cpu,value,scalar);memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_xor(CPU,unsigned dest,unsigned left,unsigned right) {
    unsigned char *dst=reg(cpu,dest),a[16],b[16];
    memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);
    for(unsigned i=0;i<16;i++)dst[i]=a[i]^b[i];
    memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_xormem(CPU,unsigned dest,unsigned left,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),a[16],b[16];
    memcpy(a,reg(cpu,left),16);memcpy(b,memory(cpu,address,TB_SSE_U128),16);
    for(unsigned i=0;i<16;i++)dst[i]=a[i]^b[i];
    memset(cpu->_ymm_hi[dest],0,16);
}

static void sqrt64(CPU,unsigned char value[16],const unsigned char source[8]) {
    uint64_t input=0,result;uint32_t raised;
    for(unsigned i=0;i<8;i++)input|=(uint64_t)source[i]<<(8*i);
    if(!tb_sse_sqrt64(input,cpu->_mxcsr,&result,&raised))abort();
    cpu->_mxcsr|=raised;if(raised&~(cpu->_mxcsr>>7))abort();
    for(unsigned i=0;i<8;i++)value[i]=(unsigned char)(result>>(8*i));
}
void tb_avx_sqrtsd(CPU,unsigned dest,unsigned merge,unsigned source) {
    unsigned char *dst=reg(cpu,dest),value[16],input[8];
    memcpy(value,reg(cpu,merge),16);memcpy(input,reg(cpu,source),8);
    sqrt64(cpu,value,input);memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_sqrtsdmem(CPU,unsigned dest,unsigned merge,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[16],input[8];
    memcpy(value,reg(cpu,merge),16);memcpy(input,memory(cpu,address,TB_SSE_Q),8);
    sqrt64(cpu,value,input);memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}

static void sd2ss(CPU,unsigned char value[16],const unsigned char source[8]) {
    unsigned raised=0;uint32_t result=double_single(read64(source),cpu->_mxcsr,&raised);
    cpu->_mxcsr|=raised;if(raised&~(cpu->_mxcsr>>7))abort();
    for(unsigned i=0;i<4;i++)value[i]=(unsigned char)(result>>(8*i));
}
void tb_avx_sd2ss(CPU,unsigned dest,unsigned merge,unsigned source) {
    unsigned char *dst=reg(cpu,dest),value[16],input[8];
    memcpy(value,reg(cpu,merge),16);memcpy(input,reg(cpu,source),8);
    sd2ss(cpu,value,input);memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_sd2ssmem(CPU,unsigned dest,unsigned merge,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[16],input[8];
    memcpy(value,reg(cpu,merge),16);memcpy(input,memory(cpu,address,TB_SSE_Q),8);
    sd2ss(cpu,value,input);memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}

static void comiss(CPU,uint32_t left,uint32_t right) {
    uint32_t a=left&0x7fffffffu,b=right&0x7fffffffu;
    unsigned raised=0,condition;
    /* Ordered comparison raises invalid for either quiet or signaling NaN.
       NaN classification suppresses denormal status from the other operand. */
    if(a>0x7f800000u || b>0x7f800000u) {raised=1;condition=ZF|PF|CF;}
    else {
        int den_a=a&&a<0x00800000u,den_b=b&&b<0x00800000u;
        if(cpu->_mxcsr&0x40) {
            if(den_a){left&=0x80000000u;a=0;}
            if(den_b){right&=0x80000000u;b=0;}
        } else if(den_a||den_b)raised=2;
        if(left==right||(!a&&!b))condition=ZF;
        else {
            uint32_t key_a=(left&0x80000000u)?~left:(left^0x80000000u);
            uint32_t key_b=(right&0x80000000u)?~right:(right^0x80000000u);
            condition=key_a<key_b?CF:0;
        }
    }
    cpu->_mxcsr|=raised;if(raised&~(cpu->_mxcsr>>7))abort();
    cpu->_eflags=(cpu->_eflags&~(OF|SF|AF|ZF|PF|CF))|condition;
}
void tb_avx_comiss(CPU,unsigned left,unsigned right) {
    uint32_t a=tb_sse_getd(cpu,left),b=tb_sse_getd(cpu,right);comiss(cpu,a,b);
}
void tb_avx_comissmem(CPU,unsigned left,uint32_t address) {
    uint32_t a=tb_sse_getd(cpu,left),b=0;
    const unsigned char *source=memory(cpu,address,TB_SSE_SS);
    for(unsigned i=0;i<4;i++)b|=(uint32_t)source[i]<<(8*i);
    comiss(cpu,a,b);
}

void tb_avx_apsreg(CPU,unsigned dest,unsigned source) {
    unsigned char *dst=reg(cpu,dest),value[16];memcpy(value,reg(cpu,source),16);
    memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_apsload(CPU,unsigned dest,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[16];memcpy(value,memory(cpu,address,TB_SSE_A128),16);
    memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_apsstore(CPU,unsigned source,uint32_t address) {
    unsigned char value[16];memcpy(value,reg(cpu,source),16);
    memcpy(memory(cpu,address,TB_SSE_A128),value,16);
}

/* Guest alignment and 32-bit range are independent of the mapped host pointer. */
static unsigned char *memory256(CPU,uint32_t address,unsigned aligned) {
    if(!cpu||(aligned&&(address&31))||(uint64_t)address+32>UINT64_C(0x100000000))abort();
    uintptr_t base=(uintptr_t)cpu->_pointer_offset;
    if(base>UINTPTR_MAX-(uint64_t)address-32)abort();
    return (unsigned char *)(base+address);
}
void tb_avx_aps256reg(CPU,unsigned dest,unsigned source) {
    unsigned char *dst=reg(cpu,dest),value[32];
    memcpy(value,reg(cpu,source),16);memcpy(value+16,cpu->_ymm_hi[source],16);
    memcpy(dst,value,16);memcpy(cpu->_ymm_hi[dest],value+16,16);
}
void tb_avx_aps256load(CPU,unsigned dest,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[32];memcpy(value,memory256(cpu,address,1),32);
    memcpy(dst,value,16);memcpy(cpu->_ymm_hi[dest],value+16,16);
}
void tb_avx_aps256store(CPU,unsigned source,uint32_t address) {
    unsigned char value[32];memcpy(value,reg(cpu,source),16);memcpy(value+16,cpu->_ymm_hi[source],16);
    memcpy(memory256(cpu,address,1),value,32);
}

void tb_avx_upsreg(CPU,unsigned dest,unsigned source) {tb_avx_apsreg(cpu,dest,source);}
void tb_avx_ups256reg(CPU,unsigned dest,unsigned source) {tb_avx_aps256reg(cpu,dest,source);}
void tb_avx_upsload(CPU,unsigned dest,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[16];memcpy(value,memory(cpu,address,TB_SSE_U128),16);
    memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_upsstore(CPU,unsigned source,uint32_t address) {
    unsigned char value[16];memcpy(value,reg(cpu,source),16);memcpy(memory(cpu,address,TB_SSE_U128),value,16);
}
void tb_avx_ups256load(CPU,unsigned dest,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[32];memcpy(value,memory256(cpu,address,0),32);
    memcpy(dst,value,16);memcpy(cpu->_ymm_hi[dest],value+16,16);
}
void tb_avx_ups256store(CPU,unsigned source,uint32_t address) {
    unsigned char value[32];memcpy(value,reg(cpu,source),16);memcpy(value+16,cpu->_ymm_hi[source],16);
    memcpy(memory256(cpu,address,0),value,32);
}

void tb_avx_zeroupper(CPU) {
    if(!cpu)abort();
    memset(cpu->_ymm_hi,0,sizeof cpu->_ymm_hi);
}

static void shufps_publish(CPU,unsigned wide,unsigned dest,const unsigned char *left,const unsigned char *right,unsigned control) {
    unsigned char value[32];
    for(unsigned block=0;block<=wide;block++)for(unsigned lane=0;lane<4;lane++)
        memcpy(value+block*16+lane*4,(lane<2?left:right)+block*16+((control>>(lane*2))&3)*4,4);
    memcpy(reg(cpu,dest),value,16);
    if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_shufps(CPU,unsigned wide,unsigned dest,unsigned left,unsigned right,unsigned control) {
    if(wide>1||control>255)abort();(void)reg(cpu,dest);
    unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(b+16,cpu->_ymm_hi[right],16);}
    shufps_publish(cpu,wide,dest,a,b,control);
}
void tb_avx_shufpsmem(CPU,unsigned wide,unsigned dest,unsigned left,uint32_t address,unsigned control) {
    if(wide>1||control>255)abort();(void)reg(cpu,dest);
    unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);
    if(wide)memcpy(a+16,cpu->_ymm_hi[left],16);
    memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);
    shufps_publish(cpu,wide,dest,a,b,control);
}

/* VEXTRACTF128 selects a half before any aliased destination publication. */
void tb_avx_extract128reg(CPU,unsigned dest,unsigned source,unsigned control) {
    if(control>255)abort();unsigned char *dst=reg(cpu,dest),*src=reg(cpu,source),value[16];
    memcpy(value,(control&1)?cpu->_ymm_hi[source]:src,16);
    memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_extract128store(CPU,unsigned source,uint32_t address,unsigned control) {
    if(control>255)abort();unsigned char *src=reg(cpu,source),value[16];
    memcpy(value,(control&1)?cpu->_ymm_hi[source]:src,16);
    memcpy(memory(cpu,address,TB_SSE_U128),value,16);
}

static void half_publish(CPU,unsigned dest,const unsigned char value[16]) {
    memcpy(reg(cpu,dest),value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_halfreg(CPU,unsigned high,unsigned dest,unsigned merge,unsigned source) {
    if(high>1)abort();(void)reg(cpu,dest);
    unsigned char a[16],b[16],value[16];memcpy(a,reg(cpu,merge),16);memcpy(b,reg(cpu,source),16);
    memcpy(value,high?a:b+8,8);memcpy(value+8,high?b:a+8,8);half_publish(cpu,dest,value);
}
void tb_avx_halfload(CPU,unsigned high,unsigned dest,unsigned merge,uint32_t address) {
    if(high>1)abort();(void)reg(cpu,dest);
    unsigned char a[16],b[8],value[16];memcpy(a,reg(cpu,merge),16);memcpy(b,memory(cpu,address,TB_SSE_Q),8);
    memcpy(value,high?a:b,8);memcpy(value+8,high?b:a+8,8);half_publish(cpu,dest,value);
}
void tb_avx_halfstore(CPU,unsigned high,unsigned source,uint32_t address) {
    if(high>1)abort();unsigned char value[8];memcpy(value,reg(cpu,source)+high*8,8);
    memcpy(memory(cpu,address,TB_SSE_Q),value,8);
}

static void packed_publish(CPU,unsigned wide,unsigned op,unsigned dest,const unsigned char *a,const unsigned char *b) {
    unsigned char value[32];uint32_t raised=0,control=cpu->_mxcsr;
    for(unsigned lane=0;lane<(wide?8:4);lane++) {
        uint32_t left,right,status;uint64_t result;
        memcpy(&left,a+lane*4,4);memcpy(&right,b+lane*4,4);
        if(!tb_sse_arithmetic(op,left,right,control,&result,&status))abort();
        raised|=status;uint32_t low=(uint32_t)result;memcpy(value+lane*4,&low,4);
    }
    cpu->_mxcsr|=raised;
    /* Precise Windows packed exception priority/delivery remains unfinished.
       Stop before any candidate lane or upper-half clearing is published. */
    if(raised&~(control>>7))abort();
    memcpy(reg(cpu,dest),value,16);
    if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_packed(CPU,unsigned wide,unsigned op,unsigned dest,unsigned left,unsigned right) {
    if(wide>1||(op!=TB_SSE_MUL32&&op!=TB_SSE_SUB32&&op!=TB_SSE_ADD32&&op!=TB_SSE_DIV32))abort();(void)reg(cpu,dest);
    unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(b+16,cpu->_ymm_hi[right],16);}
    packed_publish(cpu,wide,op,dest,a,b);
}
void tb_avx_packedmem(CPU,unsigned wide,unsigned op,unsigned dest,unsigned left,uint32_t address) {
    if(wide>1||(op!=TB_SSE_MUL32&&op!=TB_SSE_SUB32&&op!=TB_SSE_ADD32&&op!=TB_SSE_DIV32))abort();(void)reg(cpu,dest);
    unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);
    if(wide)memcpy(a+16,cpu->_ymm_hi[left],16);
    memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);
    packed_publish(cpu,wide,op,dest,a,b);
}


/* Broadcasts copy bits; they do not execute floating-point arithmetic. */
static void broadcast_publish(CPU,unsigned wide,unsigned kind,unsigned dest,const unsigned char *source) {
    unsigned bytes=4u<<kind;unsigned char value[32];
    for(unsigned offset=0;offset<(wide?32:16);offset+=bytes)memcpy(value+offset,source,bytes);
    memcpy(reg(cpu,dest),value,16);
    if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_broadcast(CPU,unsigned wide,unsigned kind,unsigned dest,unsigned source) {
    if(wide>1||kind>1||(kind&&!wide))abort();(void)reg(cpu,dest);
    unsigned char value[8];memcpy(value,reg(cpu,source),4u<<kind);
    broadcast_publish(cpu,wide,kind,dest,value);
}
void tb_avx_broadcastmem(CPU,unsigned wide,unsigned kind,unsigned dest,uint32_t address) {
    if(wide>1||kind>2||(kind&&!wide))abort();(void)reg(cpu,dest);
    unsigned char value[16];unsigned mode=kind==0?TB_SSE_SS:kind==1?TB_SSE_Q:TB_SSE_U128;
    memcpy(value,memory(cpu,address,mode),4u<<kind);
    broadcast_publish(cpu,wide,kind,dest,value);
}


void tb_avx_sdreg(CPU,unsigned dest,unsigned merge,unsigned source) {
    unsigned char *dst=reg(cpu,dest),value[16],scalar[8];
    memcpy(value,reg(cpu,merge),16);memcpy(scalar,reg(cpu,source),8);
    memcpy(value,scalar,8);memcpy(dst,value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_sdload(CPU,unsigned dest,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[8];
    memcpy(value,memory(cpu,address,TB_SSE_Q),8);
    memcpy(dst,value,8);memset(dst+8,0,8);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_sdstore(CPU,unsigned source,uint32_t address) {
    unsigned char value[8];memcpy(value,reg(cpu,source),8);
    memcpy(memory(cpu,address,TB_SSE_Q),value,8);
}

static void sqrtps_publish(CPU,unsigned wide,unsigned dest,const unsigned char *source) {
    unsigned char value[32];uint32_t raised=0,control=cpu->_mxcsr;
    for(unsigned lane=0;lane<(wide?8:4);lane++) {
        uint32_t input,status;uint64_t result;
        memcpy(&input,source+lane*4,4);
        if(!tb_sse_sqrt32(input,control,&result,&status))abort();
        raised|=status;uint32_t low=(uint32_t)result;memcpy(value+lane*4,&low,4);
    }
    cpu->_mxcsr|=raised;
    /* Preserve the complete destination if any lane has an unmasked status.
       Precise Windows exception priority/delivery remains unfinished. */
    if(raised&~(control>>7))abort();
    memcpy(reg(cpu,dest),value,16);
    if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_sqrtps(CPU,unsigned wide,unsigned dest,unsigned source) {
    if(wide>1)abort();(void)reg(cpu,dest);
    unsigned char input[32];memcpy(input,reg(cpu,source),16);
    if(wide)memcpy(input+16,cpu->_ymm_hi[source],16);
    sqrtps_publish(cpu,wide,dest,input);
}
void tb_avx_sqrtpsmem(CPU,unsigned wide,unsigned dest,uint32_t address) {
    if(wide>1)abort();(void)reg(cpu,dest);
    unsigned char input[32];
    memcpy(input,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);
    sqrtps_publish(cpu,wide,dest,input);
}

static uint32_t cvttsd2si(CPU,uint64_t bits) {
    unsigned sign=(unsigned)(bits>>63),exponent=(unsigned)((bits>>52)&2047),raised=0;
    uint64_t fraction=bits&UINT64_C(0x000fffffffffffff);uint32_t result=0;
    if(!(bits&UINT64_C(0x7fffffffffffffff))||(!exponent&&(cpu->_mxcsr&0x40)))return 0;
    int power=(int)exponent-1023;
    if(power>31) {result=0x80000000u;raised=1;}
    else if(power<0)raised=32; /* Integer conversions raise PE, not DE. */
    else {
        uint64_t significand=UINT64_C(0x0010000000000000)|fraction;
        unsigned shift=52-(unsigned)power;uint64_t whole=significand>>shift;
        /* Negative fractional values just beyond -2^31 still truncate to
           -2^31. Range-check the truncated magnitude before testing PE. */
        if(whole>(sign?UINT64_C(0x80000000):UINT64_C(0x7fffffff))) {
            result=0x80000000u;raised=1;
        } else {
            result=(uint32_t)whole;if(sign)result=0u-result;
            if(significand&((UINT64_C(1)<<shift)-1))raised=32;
        }
    }
    uint32_t control=cpu->_mxcsr;cpu->_mxcsr|=raised;
    if(raised&~(control>>7))abort();
    return result;
}
uint32_t tb_sse_cvttsd2si(CPU,unsigned source) {
    return cvttsd2si(cpu,read64(reg(cpu,source)));
}
uint32_t tb_sse_cvttsd2simem(CPU,uint32_t address) {
    return cvttsd2si(cpu,read64(memory(cpu,address,TB_SSE_Q)));
}

static void minmaxss(CPU,unsigned maximum,unsigned char value[16],uint32_t left,uint32_t right) {
    uint32_t a=left&0x7fffffffu,b=right&0x7fffffffu;unsigned raised=0;
    unsigned nan=a>0x7f800000u||b>0x7f800000u;
    unsigned den_a=a&&a<0x00800000u,den_b=b&&b<0x00800000u;
    if(cpu->_mxcsr&0x40) {
        if(den_a){left&=0x80000000u;a=0;}
        if(den_b){right&=0x80000000u;b=0;}
    }
    if(nan)raised=1; /* Includes QNaN; invalid suppresses denormal status. */
    else if(!(cpu->_mxcsr&0x40)&&(den_a||den_b))raised=2;
    uint32_t result=right;
    if(!nan&&(a||b)&&left!=right) {
        uint32_t key_a=(left&0x80000000u)?~left:(left^0x80000000u);
        uint32_t key_b=(right&0x80000000u)?~right:(right^0x80000000u);
        if(maximum?key_a>key_b:key_a<key_b)result=left;
    }
    uint32_t control=cpu->_mxcsr;cpu->_mxcsr|=raised;
    if(raised&~(control>>7))abort();
    /* Source-two NaNs, including signaling payloads, are copied unchanged. */
    memcpy(value,&result,4);
}
void tb_avx_minmaxss(CPU,unsigned maximum,unsigned dest,unsigned merge,unsigned source) {
    if(maximum>1)abort();(void)reg(cpu,dest);
    unsigned char value[16];uint32_t left,right;
    memcpy(value,reg(cpu,merge),16);memcpy(&left,value,4);memcpy(&right,reg(cpu,source),4);
    minmaxss(cpu,maximum,value,left,right);memcpy(reg(cpu,dest),value,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_minmaxssmem(CPU,unsigned maximum,unsigned dest,unsigned merge,uint32_t address) {
    if(maximum>1)abort();(void)reg(cpu,dest);
    unsigned char value[16];uint32_t left,right;
    memcpy(value,reg(cpu,merge),16);memcpy(&left,value,4);memcpy(&right,memory(cpu,address,TB_SSE_SS),4);
    minmaxss(cpu,maximum,value,left,right);memcpy(reg(cpu,dest),value,16);memset(cpu->_ymm_hi[dest],0,16);
}
static void comisd(CPU,uint64_t left,uint64_t right) {
    uint64_t a=left&UINT64_C(0x7fffffffffffffff),b=right&UINT64_C(0x7fffffffffffffff);
    unsigned raised=0,condition;
    /* Ordered comparison raises invalid for either quiet or signaling NaN.
       NaN classification suppresses denormal status from the other operand. */
    if(a>UINT64_C(0x7ff0000000000000) || b>UINT64_C(0x7ff0000000000000)) {raised=1;condition=ZF|PF|CF;}
    else {
        int den_a=a&&a<UINT64_C(0x0010000000000000),den_b=b&&b<UINT64_C(0x0010000000000000);
        if(cpu->_mxcsr&0x40) {
            if(den_a){left&=UINT64_C(0x8000000000000000);a=0;}
            if(den_b){right&=UINT64_C(0x8000000000000000);b=0;}
        } else if(den_a||den_b)raised=2;
        if(left==right||(!a&&!b))condition=ZF;
        else {
            uint64_t key_a=(left&UINT64_C(0x8000000000000000))?~left:(left^UINT64_C(0x8000000000000000));
            uint64_t key_b=(right&UINT64_C(0x8000000000000000))?~right:(right^UINT64_C(0x8000000000000000));
            condition=key_a<key_b?CF:0;
        }
    }
    cpu->_mxcsr|=raised;if(raised&~(cpu->_mxcsr>>7))abort();
    cpu->_eflags=(cpu->_eflags&~(OF|SF|AF|ZF|PF|CF))|condition;
}
void tb_avx_comisd(CPU,unsigned left,unsigned right) {
    uint64_t a=read64(reg(cpu,left)),b=read64(reg(cpu,right));comisd(cpu,a,b);
}
void tb_avx_comisdmem(CPU,unsigned left,uint32_t address) {
    uint64_t a=read64(reg(cpu,left)),b=0;
    const unsigned char *source=memory(cpu,address,TB_SSE_Q);
    for(unsigned i=0;i<8;i++)b|=(uint64_t)source[i]<<(8*i);
    comisd(cpu,a,b);
}

static void integer_logic_publish(CPU,unsigned wide,unsigned op,unsigned dest,const unsigned char *a,const unsigned char *b) {
    unsigned char value[32];
    for(unsigned i=0;i<(wide?32:16);i++)value[i]=op?(a[i]|b[i]):(a[i]^b[i]);
    memcpy(reg(cpu,dest),value,16);
    if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_integer_logic(CPU,unsigned wide,unsigned op,unsigned dest,unsigned left,unsigned right) {
    if(wide>1||op>1)abort();(void)reg(cpu,dest);
    unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(b+16,cpu->_ymm_hi[right],16);}
    integer_logic_publish(cpu,wide,op,dest,a,b);
}
void tb_avx_integer_logicmem(CPU,unsigned wide,unsigned op,unsigned dest,unsigned left,uint32_t address) {
    if(wide>1||op>1)abort();(void)reg(cpu,dest);
    unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);
    if(wide)memcpy(a+16,cpu->_ymm_hi[left],16);
    memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);
    integer_logic_publish(cpu,wide,op,dest,a,b);
}

void tb_avx_setd(CPU,unsigned dest,uint32_t value) {
    tb_sse_setd(cpu,dest,value);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_qreg(CPU,unsigned dest,unsigned source) {
    unsigned char value[8];(void)reg(cpu,dest);memcpy(value,reg(cpu,source),8);
    memcpy(reg(cpu,dest),value,8);memset(reg(cpu,dest)+8,0,8);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_smallload(CPU,unsigned quad,unsigned dest,uint32_t address) {
    if(quad>1)abort();unsigned n=quad?8:4;unsigned char value[8];unsigned char *dst=reg(cpu,dest);
    memcpy(value,memory(cpu,address,quad?TB_SSE_Q:TB_SSE_D),n);
    memcpy(dst,value,n);memset(dst+n,0,16-n);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_smallstore(CPU,unsigned quad,unsigned source,uint32_t address) {
    if(quad>1)abort();unsigned n=quad?8:4;unsigned char value[8];memcpy(value,reg(cpu,source),n);
    memcpy(memory(cpu,address,quad?TB_SSE_Q:TB_SSE_D),value,n);
}

static void sadbw_publish(CPU,unsigned wide,unsigned dest,const unsigned char *a,const unsigned char *b) {
    unsigned char value[32]={0};
    for(unsigned group=0;group<(wide?4:2);group++) {
        uint16_t sum=0;
        for(unsigned j=0;j<8;j++) {
            unsigned i=group*8+j;
            sum+=(uint16_t)(a[i]>=b[i]?a[i]-b[i]:b[i]-a[i]);
        }
        memcpy(value+group*8,&sum,2);
    }
    memcpy(reg(cpu,dest),value,16);
    if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_sadbw(CPU,unsigned wide,unsigned dest,unsigned left,unsigned right) {
    if(wide>1)abort();(void)reg(cpu,dest);
    unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(b+16,cpu->_ymm_hi[right],16);}
    sadbw_publish(cpu,wide,dest,a,b);
}
void tb_avx_sadbwmem(CPU,unsigned wide,unsigned dest,unsigned left,uint32_t address) {
    if(wide>1)abort();(void)reg(cpu,dest);
    unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);
    if(wide)memcpy(a+16,cpu->_ymm_hi[left],16);
    memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);
    sadbw_publish(cpu,wide,dest,a,b);
}

static int32_t madd_signed_word(const unsigned char *p) {
    uint16_t value;memcpy(&value,p,2);return (int32_t)value-((value&0x8000)?65536:0);
}
static void maddadd_publish(CPU,unsigned wide,unsigned op,unsigned dest,const unsigned char *a,const unsigned char *b) {
    unsigned char value[32];unsigned n=wide?32:16;
    if(op==0)for(unsigned i=0;i<n;i+=2) {
        int32_t x=(int32_t)b[i]-((b[i]&128)?256:0),y=(int32_t)b[i+1]-((b[i+1]&128)?256:0);
        int32_t sum=(int32_t)a[i]*x+(int32_t)a[i+1]*y;
        if(sum>32767)sum=32767;else if(sum< -32768)sum= -32768;
        uint16_t bits=(uint16_t)sum;memcpy(value+i,&bits,2);
    } else if(op==1)for(unsigned i=0;i<n;i+=4) {
        int64_t sum=(int64_t)madd_signed_word(a+i)*madd_signed_word(b+i)+(int64_t)madd_signed_word(a+i+2)*madd_signed_word(b+i+2);
        uint32_t bits=(uint32_t)sum;memcpy(value+i,&bits,4);
    } else if(op==2)for(unsigned i=0;i<n;i+=4) {
        uint32_t x,y;memcpy(&x,a+i,4);memcpy(&y,b+i,4);x+=y;memcpy(value+i,&x,4);
    } else for(unsigned i=0;i<n;i++)value[i]=(unsigned char)(a[i]+b[i]);
    memcpy(reg(cpu,dest),value,16);
    if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_maddadd(CPU,unsigned wide,unsigned op,unsigned dest,unsigned left,unsigned right) {
    if(wide>1||op>3)abort();(void)reg(cpu,dest);
    unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(b+16,cpu->_ymm_hi[right],16);}
    maddadd_publish(cpu,wide,op,dest,a,b);
}
void tb_avx_maddaddmem(CPU,unsigned wide,unsigned op,unsigned dest,unsigned left,uint32_t address) {
    if(wide>1||op>3)abort();(void)reg(cpu,dest);
    unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);
    if(wide)memcpy(a+16,cpu->_ymm_hi[left],16);
    memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);
    maddadd_publish(cpu,wide,op,dest,a,b);
}

static void packed_shift_publish(CPU,unsigned wide,unsigned op,unsigned dest,const unsigned char *a,const unsigned char *counts,uint64_t count) {
    unsigned char value[32]={0};unsigned n=wide?32:16;
    if(op==0||op==4)for(unsigned i=0;i<n;i+=4) {
        uint32_t x,local;memcpy(&x,a+i,4);if(op==4){memcpy(&local,counts+i,4);count=local;}
        x=count<32?x<<(unsigned)count:0;memcpy(value+i,&x,4);
    } else if(op==1)for(unsigned i=0;i<n;i+=8) {
        uint64_t x;memcpy(&x,a+i,8);x=count<64?x>>(unsigned)count:0;memcpy(value+i,&x,8);
    } else if(count<16)for(unsigned lane=0;lane<n;lane+=16)for(unsigned i=0;i<16;i++) {
        if(op==2&&i>=count)value[lane+i]=a[lane+i-(unsigned)count];
        else if(op==3&&i+count<16)value[lane+i]=a[lane+i+(unsigned)count];
    }
    memcpy(reg(cpu,dest),value,16);
    if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_shiftimm(CPU,unsigned wide,unsigned op,unsigned dest,unsigned source,unsigned count) {
    if(wide>1||op>3||count>255)abort();(void)reg(cpu,dest);unsigned char a[32];memcpy(a,reg(cpu,source),16);
    if(wide)memcpy(a+16,cpu->_ymm_hi[source],16);packed_shift_publish(cpu,wide,op,dest,a,NULL,count);
}
void tb_avx_shiftcount(CPU,unsigned wide,unsigned op,unsigned dest,unsigned source,unsigned countreg) {
    if(wide>1||(op!=0&&op!=1&&op!=4))abort();(void)reg(cpu,dest);unsigned char a[32],b[32];uint64_t count;
    memcpy(a,reg(cpu,source),16);memcpy(b,reg(cpu,countreg),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[source],16);if(op==4)memcpy(b+16,cpu->_ymm_hi[countreg],16);}
    memcpy(&count,b,8);packed_shift_publish(cpu,wide,op,dest,a,b,count);
}
void tb_avx_shiftcountmem(CPU,unsigned wide,unsigned op,unsigned dest,unsigned source,uint32_t address) {
    if(wide>1||(op!=0&&op!=1&&op!=4))abort();(void)reg(cpu,dest);unsigned char a[32],b[32];uint64_t count;
    memcpy(a,reg(cpu,source),16);if(wide)memcpy(a+16,cpu->_ymm_hi[source],16);
    memcpy(b,op==4&&wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),op==4&&wide?32:16);
    memcpy(&count,b,8);packed_shift_publish(cpu,wide,op,dest,a,b,count);
}

static void permd_publish(CPU,unsigned dest,const unsigned char *indices,const unsigned char *data) {
    unsigned char value[32];
    for(unsigned i=0;i<8;i++){uint32_t index;memcpy(&index,indices+i*4,4);memcpy(value+i*4,data+(index&7)*4,4);}
    memcpy(reg(cpu,dest),value,16);memcpy(cpu->_ymm_hi[dest],value+16,16);
}
void tb_avx_permd(CPU,unsigned dest,unsigned indices,unsigned source) {
    (void)reg(cpu,dest);unsigned char a[32],b[32];
    memcpy(a,reg(cpu,indices),16);memcpy(a+16,cpu->_ymm_hi[indices],16);
    memcpy(b,reg(cpu,source),16);memcpy(b+16,cpu->_ymm_hi[source],16);permd_publish(cpu,dest,a,b);
}
void tb_avx_permdmem(CPU,unsigned dest,unsigned indices,uint32_t address) {
    (void)reg(cpu,dest);unsigned char a[32],b[32];
    memcpy(a,reg(cpu,indices),16);memcpy(a+16,cpu->_ymm_hi[indices],16);
    memcpy(b,memory256(cpu,address,0),32);permd_publish(cpu,dest,a,b);
}

static void unpackhighq_publish(CPU,unsigned wide,unsigned dest,const unsigned char *a,const unsigned char *b) {
    unsigned char value[32];
    for(unsigned lane=0;lane<(wide?32:16);lane+=16){memcpy(value+lane,a+lane+8,8);memcpy(value+lane+8,b+lane+8,8);}
    memcpy(reg(cpu,dest),value,16);if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_unpackhighq(CPU,unsigned wide,unsigned dest,unsigned left,unsigned right) {
    if(wide>1)abort();(void)reg(cpu,dest);unsigned char a[32],b[32];
    memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(b+16,cpu->_ymm_hi[right],16);}unpackhighq_publish(cpu,wide,dest,a,b);
}
void tb_avx_unpackhighqmem(CPU,unsigned wide,unsigned dest,unsigned left,uint32_t address) {
    if(wide>1)abort();(void)reg(cpu,dest);unsigned char a[32],b[32];
    memcpy(a,reg(cpu,left),16);if(wide)memcpy(a+16,cpu->_ymm_hi[left],16);
    memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);unpackhighq_publish(cpu,wide,dest,a,b);
}

static void shufd_publish(CPU,unsigned wide,unsigned dest,const unsigned char *source,unsigned order) {
    unsigned char value[32];
    for(unsigned lane=0;lane<(wide?32:16);lane+=16)for(unsigned j=0;j<4;j++)memcpy(value+lane+j*4,source+lane+((order>>(j*2))&3)*4,4);
    memcpy(reg(cpu,dest),value,16);if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_shufd(CPU,unsigned wide,unsigned dest,unsigned source,unsigned order) {
    if(wide>1||order>255)abort();(void)reg(cpu,dest);unsigned char a[32];memcpy(a,reg(cpu,source),16);
    if(wide)memcpy(a+16,cpu->_ymm_hi[source],16);shufd_publish(cpu,wide,dest,a,order);
}
void tb_avx_shufdmem(CPU,unsigned wide,unsigned dest,uint32_t address,unsigned order) {
    if(wide>1||order>255)abort();(void)reg(cpu,dest);unsigned char a[32];
    memcpy(a,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);shufd_publish(cpu,wide,dest,a,order);
}

static void shufb_publish(CPU,unsigned wide,unsigned dest,const unsigned char *data,const unsigned char *control) {
    unsigned char value[32];
    for(unsigned j=0;j<(wide?32:16);j++)value[j]=(control[j]&0x80)?0:data[(j&~15u)+(control[j]&15)];
    memcpy(reg(cpu,dest),value,16);if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_shufb(CPU,unsigned wide,unsigned dest,unsigned source,unsigned controls) {
    if(wide>1)abort();(void)reg(cpu,dest);unsigned char a[32],b[32];memcpy(a,reg(cpu,source),16);memcpy(b,reg(cpu,controls),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[source],16);memcpy(b+16,cpu->_ymm_hi[controls],16);}shufb_publish(cpu,wide,dest,a,b);
}
void tb_avx_shufbmem(CPU,unsigned wide,unsigned dest,unsigned source,uint32_t address) {
    if(wide>1)abort();(void)reg(cpu,dest);unsigned char a[32],b[32];memcpy(a,reg(cpu,source),16);if(wide)memcpy(a+16,cpu->_ymm_hi[source],16);
    memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);shufb_publish(cpu,wide,dest,a,b);
}

static void ibroadcast_publish(CPU,unsigned wide,unsigned kind,unsigned dest,const unsigned char *source) {
    unsigned bytes=2u<<kind;unsigned char value[32];
    for(unsigned j=0;j<(wide?32:16);j+=bytes)memcpy(value+j,source,bytes);
    memcpy(reg(cpu,dest),value,16);if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_ibroadcast(CPU,unsigned wide,unsigned kind,unsigned dest,unsigned source) {
    if(wide>1||kind>2)abort();(void)reg(cpu,dest);unsigned char value[8];memcpy(value,reg(cpu,source),2u<<kind);ibroadcast_publish(cpu,wide,kind,dest,value);
}
void tb_avx_ibroadcastmem(CPU,unsigned wide,unsigned kind,unsigned dest,uint32_t address) {
    if(wide>1||kind>3||(kind==3&&!wide))abort();(void)reg(cpu,dest);unsigned char value[16];memcpy(value,memory(cpu,address,TB_SSE_U128),2u<<kind);ibroadcast_publish(cpu,wide,kind,dest,value);
}

static void icmp_publish(CPU,unsigned wide,unsigned op,unsigned dest,const unsigned char *a,const unsigned char *b) {
    unsigned char value[32];unsigned n=wide?32:16;
    if(op==2)for(unsigned j=0;j<n;j+=2){uint16_t x,y,v;memcpy(&x,a+j,2);memcpy(&y,b+j,2);v=x==y?UINT16_MAX:0;memcpy(value+j,&v,2);}
    else for(unsigned j=0;j<n;j++){int x=a[j]<128?a[j]:(int)a[j]-256,y=b[j]<128?b[j]:(int)b[j]-256;value[j]=(op==0?x>y:a[j]==b[j])?255:0;}
    memcpy(reg(cpu,dest),value,16);if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_icmp(CPU,unsigned wide,unsigned op,unsigned dest,unsigned left,unsigned right) {
    if(wide>1||op>2)abort();(void)reg(cpu,dest);unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(b+16,cpu->_ymm_hi[right],16);}icmp_publish(cpu,wide,op,dest,a,b);
}
void tb_avx_icmpmem(CPU,unsigned wide,unsigned op,unsigned dest,unsigned left,uint32_t address) {
    if(wide>1||op>2)abort();(void)reg(cpu,dest);unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);if(wide)memcpy(a+16,cpu->_ymm_hi[left],16);
    memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);icmp_publish(cpu,wide,op,dest,a,b);
}

static void blendvb_publish(CPU,unsigned wide,unsigned dest,const unsigned char *a,const unsigned char *b,const unsigned char *mask) {
    unsigned char value[32];for(unsigned j=0;j<(wide?32:16);j++)value[j]=(mask[j]&0x80)?b[j]:a[j];
    memcpy(reg(cpu,dest),value,16);if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_blendvb(CPU,unsigned wide,unsigned dest,unsigned left,unsigned right,unsigned masks) {
    if(wide>1)abort();(void)reg(cpu,dest);unsigned char a[32],b[32],m[32];memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);memcpy(m,reg(cpu,masks),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(b+16,cpu->_ymm_hi[right],16);memcpy(m+16,cpu->_ymm_hi[masks],16);}blendvb_publish(cpu,wide,dest,a,b,m);
}
void tb_avx_blendvbmem(CPU,unsigned wide,unsigned dest,unsigned left,uint32_t address,unsigned masks) {
    if(wide>1)abort();(void)reg(cpu,dest);unsigned char a[32],b[32],m[32];memcpy(a,reg(cpu,left),16);memcpy(m,reg(cpu,masks),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(m+16,cpu->_ymm_hi[masks],16);}memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);blendvb_publish(cpu,wide,dest,a,b,m);
}

static void inserti128_publish(CPU,unsigned dest,const unsigned char *merge,const unsigned char *insert,unsigned control) {
    unsigned char value[32];memcpy(value,merge,32);memcpy(value+(control&1)*16,insert,16);
    memcpy(reg(cpu,dest),value,16);memcpy(cpu->_ymm_hi[dest],value+16,16);
}
void tb_avx_inserti128(CPU,unsigned dest,unsigned merge,unsigned source,unsigned control) {
    if(control>255)abort();(void)reg(cpu,dest);unsigned char a[32],b[16];memcpy(a,reg(cpu,merge),16);memcpy(a+16,cpu->_ymm_hi[merge],16);memcpy(b,reg(cpu,source),16);
    inserti128_publish(cpu,dest,a,b,control);
}
void tb_avx_inserti128mem(CPU,unsigned dest,unsigned merge,uint32_t address,unsigned control) {
    if(control>255)abort();(void)reg(cpu,dest);unsigned char a[32],b[16];memcpy(a,reg(cpu,merge),16);memcpy(a+16,cpu->_ymm_hi[merge],16);memcpy(b,memory(cpu,address,TB_SSE_U128),16);
    inserti128_publish(cpu,dest,a,b,control);
}

uint32_t tb_avx_movmskb(CPU,unsigned wide,unsigned source) {
    if(wide>1)abort();uint32_t value=tb_sse_mask(cpu,source);
    if(wide)for(unsigned j=0;j<16;j++)value|=(uint32_t)(cpu->_ymm_hi[source][j]>>7)<<(j+16);
    return value;
}

static void subusw_publish(CPU,unsigned wide,unsigned dest,const unsigned char *a,const unsigned char *b) {
    unsigned char value[32];for(unsigned j=0;j<(wide?32:16);j+=2){uint16_t x,y,v;memcpy(&x,a+j,2);memcpy(&y,b+j,2);v=x>y?(uint16_t)(x-y):0;memcpy(value+j,&v,2);}
    memcpy(reg(cpu,dest),value,16);if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_subusw(CPU,unsigned wide,unsigned dest,unsigned left,unsigned right) {
    if(wide>1)abort();(void)reg(cpu,dest);unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(b+16,cpu->_ymm_hi[right],16);}subusw_publish(cpu,wide,dest,a,b);
}
void tb_avx_subuswmem(CPU,unsigned wide,unsigned dest,unsigned left,uint32_t address) {
    if(wide>1)abort();(void)reg(cpu,dest);unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);if(wide)memcpy(a+16,cpu->_ymm_hi[left],16);
    memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);subusw_publish(cpu,wide,dest,a,b);
}

/* Polynomial multiplication over GF(2), independently in each 128-bit lane.
   Only imm8 bits 0 and 4 select input quadwords. Snapshot before publication
   so every destination/source alias observes the original input registers. */
static void clmul_publish(CPU,unsigned wide,unsigned dest,const unsigned char *a,const unsigned char *b,unsigned control) {
    unsigned char value[32];
    for(unsigned lane=0;lane<(wide?2u:1u);lane++) {
        uint64_t x,y;memcpy(&x,a+lane*16+(control&1)*8,8);memcpy(&y,b+lane*16+((control>>4)&1)*8,8);
        __uint128_t product=0;
        for(unsigned bit=0;bit<64;bit++)if((y>>bit)&1)product^=(__uint128_t)x<<bit;
        memcpy(value+lane*16,&product,16);
    }
    memcpy(reg(cpu,dest),value,16);if(wide)memcpy(cpu->_ymm_hi[dest],value+16,16);else memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_clmul(CPU,unsigned wide,unsigned dest,unsigned left,unsigned right,unsigned control) {
    if(wide>1||control>255)abort();(void)reg(cpu,dest);unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);memcpy(b,reg(cpu,right),16);
    if(wide){memcpy(a+16,cpu->_ymm_hi[left],16);memcpy(b+16,cpu->_ymm_hi[right],16);}clmul_publish(cpu,wide,dest,a,b,control);
}
void tb_avx_clmulmem(CPU,unsigned wide,unsigned dest,unsigned left,uint32_t address,unsigned control) {
    if(wide>1||control>255)abort();(void)reg(cpu,dest);unsigned char a[32],b[32];memcpy(a,reg(cpu,left),16);if(wide)memcpy(a+16,cpu->_ymm_hi[left],16);
    memcpy(b,wide?memory256(cpu,address,0):memory(cpu,address,TB_SSE_U128),wide?32:16);clmul_publish(cpu,wide,dest,a,b,control);
}

/* Dword extraction reads a source lane without changing any vector bytes.
   Only imm8 bits0..1 select the lane. Memory destinations store exactly4 bytes. */
uint32_t tb_avx_extractd(CPU,unsigned source,unsigned control) {
    if(control>255)abort();uint32_t value;memcpy(&value,reg(cpu,source)+(control&3)*4,4);return value;
}
void tb_avx_extractdmem(CPU,unsigned source,uint32_t address,unsigned control) {
    uint32_t value=tb_avx_extractd(cpu,source,control);memcpy(memory(cpu,address,TB_SSE_D),&value,4);
}
void tb_avx_insertd(CPU,unsigned dest,unsigned merge,uint32_t value,unsigned control) {
    if(control>255)abort();(void)reg(cpu,dest);unsigned char result[16];memcpy(result,reg(cpu,merge),16);
    memcpy(result+(control&3)*4,&value,4);memcpy(reg(cpu,dest),result,16);memset(cpu->_ymm_hi[dest],0,16);
}
void tb_avx_insertdmem(CPU,unsigned dest,unsigned merge,uint32_t address,unsigned control) {
    if(control>255)abort();uint32_t value;memcpy(&value,memory(cpu,address,TB_SSE_D),4);tb_avx_insertd(cpu,dest,merge,value,control);
}

/* Legacy XMM word extraction zero-extends a REG result and preserves all
   vector state. SSE4.1 memory destinations write exactly two bytes. */
uint32_t tb_sse_extractw(CPU,unsigned source,unsigned control) {
    if(control>255)abort();uint16_t value;memcpy(&value,reg(cpu,source)+(control&7)*2,2);return value;
}
void tb_sse_extractwmem(CPU,unsigned source,uint32_t address,unsigned control) {
    uint16_t value=(uint16_t)tb_sse_extractw(cpu,source,control);
    if((uint64_t)address+2>UINT64_C(0x100000000))abort();
    uintptr_t base=(uintptr_t)cpu->_pointer_offset;if(base>UINTPTR_MAX-(uint64_t)address-2)abort();
    memcpy((void *)(base+address),&value,2);
}

/* System-wide completion conservatively orders both loads and stores for all
   three guest fences. LFENCE also blocks subsequent speculative execution.
   Memory clobbers keep compiler memory accesses on their respective sides.
   This is intentionally stronger than SFENCE/LFENCE store-order requirements. */
void tb_sse_fence(CPU,unsigned kind) {
    if(!cpu||kind>2)abort();
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ volatile("dsb sy" ::: "memory");
    if(kind==0)__asm__ volatile("isb" ::: "memory");
#else
#error TribesPad native fence mapping requires ARM64
#endif
}

/* Condition numbers follow x86 opcode0F40..4F. Conditional moves preserve
   flags and the high destination half for16-bit operands. */
static unsigned cmov_condition(unsigned cc,uint32_t flags) {
    unsigned c=!!(flags&CF),p=!!(flags&PF),z=!!(flags&ZF),s=!!(flags&SF),o=!!(flags&OF);
    unsigned value;
    switch(cc>>1){case 0:value=o;break;case 1:value=c;break;case 2:value=z;break;case 3:value=c|z;break;case 4:value=s;break;case 5:value=p;break;case 6:value=s^o;break;default:value=z|(s^o);break;}
    return value^(cc&1);
}
uint32_t tb_sse_cmov(CPU,unsigned cc,unsigned bits,unsigned dest,uint32_t value) {
    if(!cpu||cc>15||(bits!=16&&bits!=32)||dest>7)abort();
    uint32_t old;memcpy(&old,(const unsigned char *)cpu+dest*4,4);
    if(!cmov_condition(cc,cpu->_eflags))return old;
    return bits==16?(old&0xffff0000u)|(value&0xffffu):value;
}
uint32_t tb_sse_cmovmem(CPU,unsigned cc,unsigned bits,unsigned dest,uint32_t address) {
    if(!cpu||cc>15||(bits!=16&&bits!=32)||dest>7)abort();unsigned n=bits/8;
    if((uint64_t)address+n>UINT64_C(0x100000000))abort();uintptr_t base=(uintptr_t)cpu->_pointer_offset;
    if(base>UINTPTR_MAX-(uint64_t)address-n)abort();
    /* Volatile byte accesses force the architectural source read even when
       the condition is false, and permit unaligned guest operands. */
    const volatile unsigned char *src=(const volatile unsigned char *)(base+address);uint32_t value=0;
    for(unsigned k=0;k<n;++k)value|=(uint32_t)src[k]<<(8*k);
    return tb_sse_cmov(cpu,cc,bits,dest,value);
}

/* Packed legacy widening snapshots two source singles and publishes both
   doubles together. Destination YMM upper128 remains unchanged. */
static void ps2pd_publish(CPU,unsigned dest,const unsigned char source[8]) {
    uint64_t result[2];unsigned raised=0;
    for(unsigned lane=0;lane<2;++lane){uint32_t input;unsigned lane_raised;memcpy(&input,source+lane*4,4);result[lane]=single_double(input,cpu->_mxcsr,&lane_raised);raised|=lane_raised;}
    cpu->_mxcsr|=raised;
    /* Precise unmasked packed priority/Windows delivery remain unfinished. */
    if(raised&~(cpu->_mxcsr>>7))abort();memcpy(reg(cpu,dest),result,16);
}
void tb_sse_ps2pd(CPU,unsigned dest,unsigned source) {
    (void)reg(cpu,dest);unsigned char input[8];memcpy(input,reg(cpu,source),8);ps2pd_publish(cpu,dest,input);
}
void tb_sse_ps2pdmem(CPU,unsigned dest,uint32_t address) {
    (void)reg(cpu,dest);unsigned char input[8];memcpy(input,memory(cpu,address,TB_SSE_Q),8);ps2pd_publish(cpu,dest,input);
}

void tb_sse_sqrtsd(CPU,unsigned dest,unsigned source) {
    unsigned char *dst=reg(cpu,dest),value[16],input[8];
    memcpy(input,reg(cpu,source),8);memcpy(value,dst,16);
    sqrt64(cpu,value,input);memcpy(dst,value,8);
}
void tb_sse_sqrtsdmem(CPU,unsigned dest,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[16],input[8];
    memcpy(input,memory(cpu,address,TB_SSE_Q),8);memcpy(value,dst,16);
    sqrt64(cpu,value,input);memcpy(dst,value,8);
}

void tb_sse_sd2ss(CPU,unsigned dest,unsigned source) {
    unsigned char *dst=reg(cpu,dest),value[16],input[8];
    memcpy(input,reg(cpu,source),8);memcpy(value,dst,16);
    sd2ss(cpu,value,input);memcpy(dst,value,4);
}
void tb_sse_sd2ssmem(CPU,unsigned dest,uint32_t address) {
    unsigned char *dst=reg(cpu,dest),value[16],input[8];
    memcpy(input,memory(cpu,address,TB_SSE_Q),8);memcpy(value,dst,16);
    sd2ss(cpu,value,input);memcpy(dst,value,4);
}

static void legacy_mulps_publish(CPU,unsigned dest,const unsigned char left[16],const unsigned char right[16]) {
    unsigned char value[16];uint32_t raised=0,control=cpu->_mxcsr;
    for(unsigned lane=0;lane<4;++lane){uint32_t a,b,status;uint64_t result;memcpy(&a,left+lane*4,4);memcpy(&b,right+lane*4,4);if(!tb_sse_arithmetic(TB_SSE_MUL32,a,b,control,&result,&status))abort();raised|=status;uint32_t low=(uint32_t)result;memcpy(value+lane*4,&low,4);}
    cpu->_mxcsr|=raised;
    /* Precise unmasked packed priority/Windows delivery remain unfinished. */
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),value,16);
}
void tb_sse_mulps(CPU,unsigned dest,unsigned source) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,reg(cpu,source),16);legacy_mulps_publish(cpu,dest,a,b);
}
void tb_sse_mulpsmem(CPU,unsigned dest,uint32_t address) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,memory(cpu,address,TB_SSE_A128),16);legacy_mulps_publish(cpu,dest,a,b);
}

static void legacy_divps_publish(CPU,unsigned dest,const unsigned char left[16],const unsigned char right[16]) {
    unsigned char value[16];uint32_t raised=0,control=cpu->_mxcsr;
    for(unsigned lane=0;lane<4;++lane){uint32_t a,b,status;uint64_t result;memcpy(&a,left+lane*4,4);memcpy(&b,right+lane*4,4);if(!tb_sse_arithmetic(TB_SSE_DIV32,a,b,control,&result,&status))abort();raised|=status;uint32_t low=(uint32_t)result;memcpy(value+lane*4,&low,4);}
    cpu->_mxcsr|=raised;
    /* Precise unmasked packed priority/Windows delivery remain unfinished. */
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),value,16);
}
void tb_sse_divps(CPU,unsigned dest,unsigned source) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,reg(cpu,source),16);legacy_divps_publish(cpu,dest,a,b);
}
void tb_sse_divpsmem(CPU,unsigned dest,uint32_t address) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,memory(cpu,address,TB_SSE_A128),16);legacy_divps_publish(cpu,dest,a,b);
}

static void legacy_addps_publish(CPU,unsigned dest,const unsigned char left[16],const unsigned char right[16]) {
    unsigned char value[16];uint32_t raised=0,control=cpu->_mxcsr;
    for(unsigned lane=0;lane<4;++lane){uint32_t a,b,status;uint64_t result;memcpy(&a,left+lane*4,4);memcpy(&b,right+lane*4,4);if(!tb_sse_arithmetic(TB_SSE_ADD32,a,b,control,&result,&status))abort();raised|=status;uint32_t low=(uint32_t)result;memcpy(value+lane*4,&low,4);}
    cpu->_mxcsr|=raised;
    /* Precise unmasked packed priority/Windows delivery remain unfinished. */
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),value,16);
}
void tb_sse_addps(CPU,unsigned dest,unsigned source) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,reg(cpu,source),16);legacy_addps_publish(cpu,dest,a,b);
}
void tb_sse_addpsmem(CPU,unsigned dest,uint32_t address) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,memory(cpu,address,TB_SSE_A128),16);legacy_addps_publish(cpu,dest,a,b);
}

static void legacy_shufps_publish(CPU,unsigned dest,const unsigned char a[16],const unsigned char b[16],unsigned control) {
    unsigned char value[16];
    for(unsigned lane=0;lane<4;++lane)memcpy(value+lane*4,(lane<2?a:b)+((control>>(lane*2))&3)*4,4);
    memcpy(reg(cpu,dest),value,16);
}
void tb_sse_shufps(CPU,unsigned dest,unsigned source,unsigned control) {
    if(control>255)abort();unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,reg(cpu,source),16);legacy_shufps_publish(cpu,dest,a,b,control);
}
void tb_sse_shufpsmem(CPU,unsigned dest,uint32_t address,unsigned control) {
    if(control>255)abort();unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);
    /* SHUFPS has a complete m128 operand even when selected lanes repeat. */
    const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);
    for(unsigned i=0;i<16;++i)b[i]=source[i];
    legacy_shufps_publish(cpu,dest,a,b,control);
}

static void legacy_subps_publish(CPU,unsigned dest,const unsigned char left[16],const unsigned char right[16]) {
    unsigned char value[16];uint32_t raised=0,control=cpu->_mxcsr;
    for(unsigned lane=0;lane<4;++lane){uint32_t a,b,status;uint64_t result;memcpy(&a,left+lane*4,4);memcpy(&b,right+lane*4,4);if(!tb_sse_arithmetic(TB_SSE_SUB32,a,b,control,&result,&status))abort();raised|=status;uint32_t low=(uint32_t)result;memcpy(value+lane*4,&low,4);}
    cpu->_mxcsr|=raised;
    /* Precise unmasked packed priority/Windows delivery remain unfinished. */
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),value,16);
}
void tb_sse_subps(CPU,unsigned dest,unsigned source) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,reg(cpu,source),16);legacy_subps_publish(cpu,dest,a,b);
}
void tb_sse_subpsmem(CPU,unsigned dest,uint32_t address) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,memory(cpu,address,TB_SSE_A128),16);legacy_subps_publish(cpu,dest,a,b);
}


void tb_sse_halfreg(CPU,unsigned high,unsigned dest,unsigned source) {
    if(high>1)abort();unsigned char value[8];
    memcpy(value,reg(cpu,source)+(high?0:8),8);
    memcpy(reg(cpu,dest)+(high?8:0),value,8);
}

void tb_sse_ss2sd(CPU,unsigned dest,unsigned source) {
    unsigned char value[16],input[4];
    memcpy(input,reg(cpu,source),4);ss2sd(cpu,value,input);memcpy(reg(cpu,dest),value,8);
}
void tb_sse_ss2sdmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[16],input[4];
    memcpy(input,memory(cpu,address,TB_SSE_SS),4);ss2sd(cpu,value,input);memcpy(reg(cpu,dest),value,8);
}

static void legacy_sqrtps_publish(CPU,unsigned dest,const unsigned char source[16]) {
    unsigned char value[16];uint32_t raised=0,control=cpu->_mxcsr;
    for(unsigned lane=0;lane<4;++lane){uint32_t input,status;uint64_t result;memcpy(&input,source+lane*4,4);if(!tb_sse_sqrt32(input,control,&result,&status))abort();raised|=status;uint32_t low=(uint32_t)result;memcpy(value+lane*4,&low,4);}
    cpu->_mxcsr|=raised;
    /* Precise unmasked packed priority/Windows delivery remain unfinished. */
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),value,16);
}
void tb_sse_sqrtps(CPU,unsigned dest,unsigned source) {
    unsigned char input[16];memcpy(input,reg(cpu,source),16);legacy_sqrtps_publish(cpu,dest,input);
}
void tb_sse_sqrtpsmem(CPU,unsigned dest,uint32_t address) {
    unsigned char input[16];memcpy(input,memory(cpu,address,TB_SSE_A128),16);legacy_sqrtps_publish(cpu,dest,input);
}

void tb_sse_minmaxss(CPU,unsigned maximum,unsigned dest,unsigned source) {
    if(maximum>1)abort();unsigned char value[16];uint32_t left,right;
    memcpy(&left,reg(cpu,dest),4);memcpy(&right,reg(cpu,source),4);
    minmaxss(cpu,maximum,value,left,right);memcpy(reg(cpu,dest),value,4);
}
void tb_sse_minmaxssmem(CPU,unsigned maximum,unsigned dest,uint32_t address) {
    if(maximum>1)abort();unsigned char value[16];uint32_t left,right;
    memcpy(&left,reg(cpu,dest),4);memcpy(&right,memory(cpu,address,TB_SSE_SS),4);
    minmaxss(cpu,maximum,value,left,right);memcpy(reg(cpu,dest),value,4);
}

/* Legacy ANDNPS inverts the old destination, then ANDs the source. Snapshot
   both operands before publication, including self aliases. */
static void legacy_andnps_publish(CPU,unsigned dest,const unsigned char source[16]) {
    unsigned char left[16],value[16];memcpy(left,reg(cpu,dest),16);
    for(unsigned i=0;i<16;++i)value[i]=(unsigned char)(~left[i]&source[i]);
    memcpy(reg(cpu,dest),value,16);
}
void tb_sse_andnps(CPU,unsigned dest,unsigned source) {
    unsigned char value[16];memcpy(value,reg(cpu,source),16);legacy_andnps_publish(cpu,dest,value);
}
void tb_sse_andnpsmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[16];const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);
    /* A zero result must not suppress any of the 16 architectural reads. */
    for(unsigned i=0;i<16;++i)value[i]=source[i];
    legacy_andnps_publish(cpu,dest,value);
}

/* Legacy SSE2 interleaves old destination/source low dwords. Upper source
   halves are ignored; destination upper dwords are replaced, YMMhi preserved. */
static void legacy_unpacklowd_publish(CPU,unsigned dest,const unsigned char source[8]) {
    unsigned char left[8],value[16];memcpy(left,reg(cpu,dest),8);
    for(unsigned lane=0;lane<2;++lane){memcpy(value+lane*8,left+lane*4,4);memcpy(value+lane*8+4,source+lane*4,4);}
    memcpy(reg(cpu,dest),value,16);
}
void tb_sse_unpacklowd(CPU,unsigned dest,unsigned source) {
    unsigned char value[8];memcpy(value,reg(cpu,source),8);legacy_unpacklowd_publish(cpu,dest,value);
}
void tb_sse_unpacklowdmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[8];const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);
    /* Intel permits fetching only low64; still validate the aligned m128
       guest range. Read all eight used bytes before publishing destination. */
    for(unsigned i=0;i<8;++i)value[i]=source[i];
    legacy_unpacklowd_publish(cpu,dest,value);
}

void tb_sse_shiftrbytes(CPU,unsigned dest,unsigned count) {
    if(count>255)abort();unsigned char value[16]={0},source[16];
    memcpy(source,reg(cpu,dest),16);
    if(count<16)memcpy(value,source+count,16-count);
    memcpy(reg(cpu,dest),value,16);
}

static void legacy_mulsd_publish(CPU,unsigned dest,uint64_t right) {
    uint64_t left,value;uint32_t raised,control=cpu->_mxcsr;memcpy(&left,reg(cpu,dest),8);
    if(!tb_sse_multiply64(left,right,control,&value,&raised))abort();cpu->_mxcsr|=raised;
    /* Windows #XM delivery remains unfinished; do not publish at this stop. */
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),&value,8);
}
void tb_sse_mulsd(CPU,unsigned dest,unsigned source) {
    uint64_t right;memcpy(&right,reg(cpu,source),8);legacy_mulsd_publish(cpu,dest,right);
}
void tb_sse_mulsdmem(CPU,unsigned dest,uint32_t address) {
    unsigned char bytes[8];const volatile unsigned char *source=memory(cpu,address,TB_SSE_Q);
    for(unsigned i=0;i<8;++i)bytes[i]=source[i];uint64_t right;memcpy(&right,bytes,8);legacy_mulsd_publish(cpu,dest,right);
}

static uint32_t legacy_signed32_single(uint32_t input,uint32_t control,unsigned *raised) {
    unsigned sign=input>>31;uint32_t magnitude=sign?0u-input:input;if(!magnitude)return 0;
    unsigned top=31u-(unsigned)__builtin_clz(magnitude);uint32_t whole;
    if(top<=23)whole=magnitude<<(23-top);
    else {
        unsigned shift=top-23;whole=magnitude>>shift;uint32_t lost=magnitude&((1u<<shift)-1);unsigned mode=(control>>13)&3;
        if(lost){*raised|=32;uint32_t half=1u<<(shift-1);if((mode==0&&(lost>half||(lost==half&&(whole&1))))||(mode==1&&sign)||(mode==2&&!sign))++whole;}
        if(whole==(1u<<24)){whole>>=1;++top;}
    }
    return (sign<<31)|((top+127)<<23)|(whole&0x7fffff);
}
static void legacy_dq2ps_publish(CPU,unsigned dest,const unsigned char source[16]) {
    uint32_t value[4];unsigned raised=0;uint32_t control=cpu->_mxcsr;
    for(unsigned lane=0;lane<4;++lane){uint32_t input;memcpy(&input,source+lane*4,4);value[lane]=legacy_signed32_single(input,control,&raised);}
    cpu->_mxcsr|=raised;
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),value,16);
}
void tb_sse_dq2ps(CPU,unsigned dest,unsigned source) {
    unsigned char value[16];memcpy(value,reg(cpu,source),16);legacy_dq2ps_publish(cpu,dest,value);
}
void tb_sse_dq2psmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[16];const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);
    for(unsigned i=0;i<16;++i)value[i]=source[i];legacy_dq2ps_publish(cpu,dest,value);
}

void tb_sse_si2ss(CPU,unsigned dest,uint32_t input) {
    unsigned raised=0;uint32_t control=cpu->_mxcsr,value=legacy_signed32_single(input,control,&raised);
    cpu->_mxcsr|=raised;
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),&value,4);
}
void tb_sse_si2ssmem(CPU,unsigned dest,uint32_t address) {
    unsigned char bytes[4];const volatile unsigned char *source=memory(cpu,address,TB_SSE_SS);
    for(unsigned i=0;i<4;++i)bytes[i]=source[i];uint32_t input;memcpy(&input,bytes,4);tb_sse_si2ss(cpu,dest,input);
}

void tb_sse_si2sd(CPU,unsigned dest,uint32_t input) {
    unsigned sign=input>>31;uint32_t magnitude=sign?0u-input:input;uint64_t value=0;
    /* All signed32 inputs are exactly representable in binary64. */
    if(magnitude){unsigned top=31u-(unsigned)__builtin_clz(magnitude);value=((uint64_t)sign<<63)|((uint64_t)(top+1023)<<52)|(((uint64_t)magnitude<<(52-top))&UINT64_C(0x000fffffffffffff));}
    memcpy(reg(cpu,dest),&value,8);
}
void tb_sse_si2sdmem(CPU,unsigned dest,uint32_t address) {
    unsigned char bytes[4];const volatile unsigned char *source=memory(cpu,address,TB_SSE_SS);
    for(unsigned i=0;i<4;++i)bytes[i]=source[i];uint32_t input;memcpy(&input,bytes,4);tb_sse_si2sd(cpu,dest,input);
}

static void legacy_packuswb_publish(CPU,unsigned dest,const unsigned char source[16]) {
    unsigned char left[16],value[16];memcpy(left,reg(cpu,dest),16);
    for(unsigned lane=0;lane<16;++lane){uint16_t input;memcpy(&input,(lane<8?left:source)+(lane%8)*2,2);value[lane]=(unsigned char)(input>32767?0:input>255?255:input);}
    memcpy(reg(cpu,dest),value,16);
}
void tb_sse_packuswb(CPU,unsigned dest,unsigned source) {
    unsigned char value[16];memcpy(value,reg(cpu,source),16);legacy_packuswb_publish(cpu,dest,value);
}
void tb_sse_packuswbmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[16];const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);
    for(unsigned i=0;i<16;++i)value[i]=source[i];legacy_packuswb_publish(cpu,dest,value);
}

static void legacy_paddd_publish(CPU,unsigned dest,const unsigned char source[16]) {
    uint32_t value[4];
    for(unsigned lane=0;lane<4;++lane){uint32_t left,right;memcpy(&left,reg(cpu,dest)+lane*4,4);memcpy(&right,source+lane*4,4);value[lane]=left+right;}
    memcpy(reg(cpu,dest),value,16);
}
void tb_sse_paddd(CPU,unsigned dest,unsigned source) {
    unsigned char value[16];memcpy(value,reg(cpu,source),16);legacy_paddd_publish(cpu,dest,value);
}
void tb_sse_padddmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[16];const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);
    for(unsigned i=0;i<16;++i)value[i]=source[i];legacy_paddd_publish(cpu,dest,value);
}

static void legacy_ucomiss(CPU,uint32_t left,uint32_t right) {
    uint32_t a=left&0x7fffffffu,b=right&0x7fffffffu;
    unsigned raised=0,condition;
    /* Unordered comparison raises invalid only for signaling NaNs. */
    if(a>0x7f800000u || b>0x7f800000u) {raised=((a>0x7f800000u && !(left&0x400000u)) || (b>0x7f800000u && !(right&0x400000u)));condition=ZF|PF|CF;}
    else {
        int den_a=a&&a<0x00800000u,den_b=b&&b<0x00800000u;
        if(cpu->_mxcsr&0x40) {
            if(den_a){left&=0x80000000u;a=0;}
            if(den_b){right&=0x80000000u;b=0;}
        } else if(den_a||den_b)raised=2;
        if(left==right||(!a&&!b))condition=ZF;
        else {
            uint32_t key_a=(left&0x80000000u)?~left:(left^0x80000000u);
            uint32_t key_b=(right&0x80000000u)?~right:(right^0x80000000u);
            condition=key_a<key_b?CF:0;
        }
    }
    cpu->_mxcsr|=raised;if(raised&~(cpu->_mxcsr>>7))abort();
    cpu->_eflags=(cpu->_eflags&~(OF|SF|AF|ZF|PF|CF))|condition;
}
void tb_sse_ucomiss(CPU,unsigned left,unsigned right) {
    uint32_t a=tb_sse_getd(cpu,left),b=tb_sse_getd(cpu,right);legacy_ucomiss(cpu,a,b);
}
void tb_sse_ucomissmem(CPU,unsigned left,uint32_t address) {
    unsigned char bytes[4];const volatile unsigned char *source=memory(cpu,address,TB_SSE_SS);
    for(unsigned i=0;i<4;++i)bytes[i]=source[i];uint32_t b;memcpy(&b,bytes,4);legacy_ucomiss(cpu,tb_sse_getd(cpu,left),b);
}

static void legacy_pminsd_publish(CPU,unsigned dest,const unsigned char source[16]) {
    uint32_t value[4];
    for(unsigned lane=0;lane<4;++lane){uint32_t left,right;memcpy(&left,reg(cpu,dest)+lane*4,4);memcpy(&right,source+lane*4,4);value[lane]=(left^UINT32_C(0x80000000))<(right^UINT32_C(0x80000000))?left:right;}
    memcpy(reg(cpu,dest),value,16);
}
void tb_sse_pminsd(CPU,unsigned dest,unsigned source) {
    unsigned char value[16];memcpy(value,reg(cpu,source),16);legacy_pminsd_publish(cpu,dest,value);
}
void tb_sse_pminsdmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[16];const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);
    for(unsigned i=0;i<16;++i)value[i]=source[i];legacy_pminsd_publish(cpu,dest,value);
}

static void legacy_unpackloww_publish(CPU,unsigned dest,const unsigned char source[8]) {
    unsigned char left[8],value[16];memcpy(left,reg(cpu,dest),8);
    for(unsigned lane=0;lane<4;++lane){memcpy(value+lane*4,left+lane*2,2);memcpy(value+lane*4+2,source+lane*2,2);}
    memcpy(reg(cpu,dest),value,16);
}
void tb_sse_unpackloww(CPU,unsigned dest,unsigned source) {
    unsigned char value[8];memcpy(value,reg(cpu,source),8);legacy_unpackloww_publish(cpu,dest,value);
}
void tb_sse_unpacklowwmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[8];const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);
    /* Intel permits fetching only low64; still validate the aligned m128
       guest range. Read all eight used bytes before publishing destination. */
    for(unsigned i=0;i<8;++i)value[i]=source[i];
    legacy_unpackloww_publish(cpu,dest,value);
}

static void legacy_unpacklowb_publish(CPU,unsigned dest,const unsigned char source[8]) {
    unsigned char left[8],value[16];memcpy(left,reg(cpu,dest),8);
    for(unsigned lane=0;lane<8;++lane){value[lane*2]=left[lane];value[lane*2+1]=source[lane];}
    memcpy(reg(cpu,dest),value,16);
}
void tb_sse_unpacklowb(CPU,unsigned dest,unsigned source) {
    unsigned char value[8];memcpy(value,reg(cpu,source),8);legacy_unpacklowb_publish(cpu,dest,value);
}
void tb_sse_unpacklowbmem(CPU,unsigned dest,uint32_t address) {
    unsigned char value[8];const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);
    /* Intel permits fetching only low64; still validate the aligned m128
       guest range. Read all eight used bytes before publishing destination. */
    for(unsigned i=0;i<8;++i)value[i]=source[i];
    legacy_unpacklowb_publish(cpu,dest,value);
}

static void legacy_divpd_publish(CPU,unsigned dest,const unsigned char left[16],const unsigned char right[16]) {
    unsigned char value[16];uint32_t raised=0,control=cpu->_mxcsr;
    for(unsigned lane=0;lane<2;++lane){uint64_t a,b,result;uint32_t status;memcpy(&a,left+lane*8,8);memcpy(&b,right+lane*8,8);if(!tb_sse_divide64(a,b,control,&result,&status))abort();raised|=status;memcpy(value+lane*8,&result,8);}
    cpu->_mxcsr|=raised;
    /* Precise unmasked packed priority/Windows delivery remain unfinished. */
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),value,16);
}
void tb_sse_divpd(CPU,unsigned dest,unsigned source) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,reg(cpu,source),16);legacy_divpd_publish(cpu,dest,a,b);
}
void tb_sse_divpdmem(CPU,unsigned dest,uint32_t address) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);for(unsigned i=0;i<16;++i)b[i]=source[i];legacy_divpd_publish(cpu,dest,a,b);
}

static void legacy_addpd_publish(CPU,unsigned dest,const unsigned char left[16],const unsigned char right[16]) {
    unsigned char value[16];uint32_t raised=0,control=cpu->_mxcsr;
    for(unsigned lane=0;lane<2;++lane){uint64_t a,b,result;uint32_t status;memcpy(&a,left+lane*8,8);memcpy(&b,right+lane*8,8);if(!tb_sse_arithmetic(TB_SSE_ADD64,a,b,control,&result,&status))abort();raised|=status;memcpy(value+lane*8,&result,8);}
    cpu->_mxcsr|=raised;
    /* Precise unmasked packed priority/Windows delivery remain unfinished. */
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),value,16);
}
void tb_sse_addpd(CPU,unsigned dest,unsigned source) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,reg(cpu,source),16);legacy_addpd_publish(cpu,dest,a,b);
}
void tb_sse_addpdmem(CPU,unsigned dest,uint32_t address) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);for(unsigned i=0;i<16;++i)b[i]=source[i];legacy_addpd_publish(cpu,dest,a,b);
}

static void legacy_mathpd_publish(CPU,unsigned multiply,unsigned dest,unsigned source) {
    unsigned char left[16],right[16],value[16];uint32_t control=cpu->_mxcsr,raised=0;
    memcpy(left,reg(cpu,dest),16);memcpy(right,reg(cpu,source),16);
    for(unsigned lane=0;lane<2;++lane) {
        uint64_t a,b,result;uint32_t status;memcpy(&a,left+lane*8,8);memcpy(&b,right+lane*8,8);
        int ok=multiply?tb_sse_multiply64(a,b,control,&result,&status):tb_sse_arithmetic(TB_SSE_SUB64,a,b,control,&result,&status);
        if(!ok)abort();raised|=status;memcpy(value+lane*8,&result,8);
    }
    cpu->_mxcsr|=raised;
    /* As with ADDPD/DIVPD, precise unmasked Windows delivery is unfinished.
       Stage both lanes and stop before publishing an unserviced fault. */
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),value,16);
}
void tb_sse_mulpd(CPU,unsigned dest,unsigned source) {legacy_mathpd_publish(cpu,1,dest,source);}
void tb_sse_subpd(CPU,unsigned dest,unsigned source) {legacy_mathpd_publish(cpu,0,dest,source);}
void tb_sse_insertw(CPU,unsigned dest,uint32_t value,unsigned control) {
    if(control>255)abort();uint16_t word=(uint16_t)value;memcpy(reg(cpu,dest)+(control&7)*2,&word,2);
}
void tb_sse_psubq(CPU,unsigned dest,unsigned source) {
    uint64_t left[2],right[2];memcpy(left,reg(cpu,dest),16);memcpy(right,reg(cpu,source),16);
    for(unsigned lane=0;lane<2;++lane)left[lane]-=right[lane];
    memcpy(reg(cpu,dest),left,16);
}

static uint64_t legacy_comparepd_lane(uint64_t left,uint64_t right,unsigned predicate,uint32_t control,unsigned *raised) {
    const uint64_t sign=UINT64_C(0x8000000000000000),infinity=UINT64_C(0x7ff0000000000000),quiet=UINT64_C(0x0008000000000000);
    uint64_t a=left&~sign,b=right&~sign;unsigned unordered=a>infinity||b>infinity,eq=0,lt=0;
    predicate&=7;
    if(unordered) {
        unsigned signaling=predicate==1||predicate==2||predicate==5||predicate==6;
        if(signaling || (a>infinity&&!(left&quiet)) || (b>infinity&&!(right&quiet)))*raised|=1;
    } else {
        unsigned den_a=a&&a<UINT64_C(0x0010000000000000),den_b=b&&b<UINT64_C(0x0010000000000000);
        if(control&0x40){if(den_a){left&=sign;a=0;}if(den_b){right&=sign;b=0;}}
        else if(den_a||den_b)*raised|=2;
        eq=left==right||(!a&&!b);
        uint64_t key_a=(left&sign)?~left:(left^sign),key_b=(right&sign)?~right:(right^sign);
        lt=!eq&&key_a<key_b;
    }
    unsigned result;
    switch(predicate){case 0:result=eq;break;case 1:result=lt;break;case 2:result=lt||eq;break;case 3:result=unordered;break;case 4:result=!eq;break;case 5:result=!lt;break;case 6:result=!(lt||eq);break;default:result=!unordered;break;}
    return result?UINT64_MAX:0;
}
static void legacy_cmppd_publish(CPU,unsigned dest,const unsigned char left[16],const unsigned char right[16],unsigned predicate) {
    if(predicate>255)abort();uint64_t value[2];unsigned raised=0;uint32_t control=cpu->_mxcsr;
    for(unsigned lane=0;lane<2;++lane){uint64_t a,b;memcpy(&a,left+lane*8,8);memcpy(&b,right+lane*8,8);value[lane]=legacy_comparepd_lane(a,b,predicate,control,&raised);}
    cpu->_mxcsr|=raised;if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),value,16);
}
void tb_sse_cmppd(CPU,unsigned dest,unsigned source,unsigned predicate) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);memcpy(b,reg(cpu,source),16);legacy_cmppd_publish(cpu,dest,a,b,predicate);
}
void tb_sse_cmppdmem(CPU,unsigned dest,uint32_t address,unsigned predicate) {
    unsigned char a[16],b[16];memcpy(a,reg(cpu,dest),16);const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);for(unsigned i=0;i<16;++i)b[i]=source[i];legacy_cmppd_publish(cpu,dest,a,b,predicate);
}

static void legacy_divsd_publish(CPU,unsigned dest,uint64_t right) {
    uint64_t left,value;uint32_t raised,control=cpu->_mxcsr;memcpy(&left,reg(cpu,dest),8);
    if(!tb_sse_divide64(left,right,control,&value,&raised))abort();cpu->_mxcsr|=raised;
    /* Windows #XM delivery remains unfinished; do not publish at this stop. */
    if(raised&~(control>>7))abort();memcpy(reg(cpu,dest),&value,8);
}
void tb_sse_divsd(CPU,unsigned dest,unsigned source) {
    uint64_t right;memcpy(&right,reg(cpu,source),8);legacy_divsd_publish(cpu,dest,right);
}
void tb_sse_divsdmem(CPU,unsigned dest,uint32_t address) {
    unsigned char bytes[8];const volatile unsigned char *source=memory(cpu,address,TB_SSE_Q);
    for(unsigned i=0;i<8;++i)bytes[i]=source[i];uint64_t right;memcpy(&right,bytes,8);legacy_divsd_publish(cpu,dest,right);
}

static uint32_t legacy_truncpd_lane(uint64_t bits,uint32_t control,unsigned *status) {
    unsigned sign=(unsigned)(bits>>63),exponent=(unsigned)((bits>>52)&2047),raised=0;
    uint64_t fraction=bits&UINT64_C(0x000fffffffffffff);uint32_t result=0;
    if(!(bits&UINT64_C(0x7fffffffffffffff))||(!exponent&&(control&0x40)))return 0;
    int power=(int)exponent-1023;
    if(power>31) {result=0x80000000u;raised=1;}
    else if(power<0)raised=32; /* Integer conversions raise PE, not DE. */
    else {
        uint64_t significand=UINT64_C(0x0010000000000000)|fraction;
        unsigned shift=52-(unsigned)power;uint64_t whole=significand>>shift;
        /* Negative fractional values just beyond -2^31 still truncate to
           -2^31. Range-check the truncated magnitude before testing PE. */
        if(whole>(sign?UINT64_C(0x80000000):UINT64_C(0x7fffffff))) {
            result=0x80000000u;raised=1;
        } else {
            result=(uint32_t)whole;if(sign)result=0u-result;
            if(significand&((UINT64_C(1)<<shift)-1))raised=32;
        }
    }
    *status|=raised;
    return result;
}
static void legacy_truncpd_publish(CPU,unsigned dest,const unsigned char source[16]) {
    uint32_t control=cpu->_mxcsr;unsigned raised=0;unsigned char result[16]={0};
    uint32_t low=legacy_truncpd_lane(read64(source),control,&raised);
    uint32_t high=legacy_truncpd_lane(read64(source+8),control,&raised);
    memcpy(result,&low,4);memcpy(result+4,&high,4);
    cpu->_mxcsr|=raised;if(raised&~(control>>7))abort();
    memcpy(reg(cpu,dest),result,16);
}
void tb_sse_truncpd(CPU,unsigned dest,unsigned source) {
    (void)reg(cpu,dest);unsigned char input[16];memcpy(input,reg(cpu,source),16);legacy_truncpd_publish(cpu,dest,input);
}
void tb_sse_truncpdmem(CPU,unsigned dest,uint32_t address) {
    (void)reg(cpu,dest);unsigned char input[16];const volatile unsigned char *source=memory(cpu,address,TB_SSE_A128);
    for(unsigned i=0;i<16;++i)input[i]=source[i];legacy_truncpd_publish(cpu,dest,input);
}

/* Legacy full-width bit operations. No floating classification or MXCSR effects.
   Read/snapshot the entire operand before publishing, even for absorbing masks. */
static void legacy_bits128_publish(CPU,unsigned op,unsigned dest,const unsigned char source[16]) {
    unsigned char result[16],left[16];memcpy(left,reg(cpu,dest),16);
    switch(op) {
    case 0:for(unsigned i=0;i<16;++i)result[i]=left[i]&source[i];break;
    case 1:for(unsigned i=0;i<16;++i)result[i]=left[i]|source[i];break;
    case 2:for(unsigned i=0;i<16;++i)result[i]=left[i]^source[i];break;
    case 3:for(unsigned i=0;i<16;++i)result[i]=(unsigned char)~left[i]&source[i];break;
    case 4:memcpy(result,left,8);memcpy(result+8,source,8);break;
    case 5:memcpy(result,left+8,8);memcpy(result+8,source+8,8);break;
    case 6:memcpy(result,source,16);break;
    default:abort();
    }
    memcpy(reg(cpu,dest),result,16);
}
void tb_sse_bits128(CPU,unsigned op,unsigned dest,unsigned source) {
    if(op>6)abort();unsigned char value[16];memcpy(value,reg(cpu,source),16);legacy_bits128_publish(cpu,op,dest,value);
}
void tb_sse_bits128mem(CPU,unsigned op,unsigned dest,uint32_t address) {
    if(op>6)abort();(void)reg(cpu,dest);unsigned char value[16];const volatile unsigned char *p=memory(cpu,address,TB_SSE_A128);
    for(unsigned i=0;i<16;++i)value[i]=p[i];legacy_bits128_publish(cpu,op,dest,value);
}

/* Legacy packed counts use the whole low64, never a masked scalar count.
 * XMM publication deliberately leaves the upper YMM state unchanged. */
static void legacy_shift_publish(CPU,unsigned op,unsigned dest,uint64_t count) {
    if(op>8)abort();
    unsigned char input[16],value[16]={0};memcpy(input,reg(cpu,dest),16);
    if(op==8) {
        if(count<16)memcpy(value+(unsigned)count,input,16-(unsigned)count);
    } else {
        unsigned width=op==0||op==3||op==6?16:op==1||op==4||op==7?32:64;
        unsigned bytes=width/8;
        for(unsigned i=0;i<16;i+=bytes) {
            uint64_t x=0,y=0;memcpy(&x,input+i,bytes);
            if(op<3) { if(count<width)y=x<<(unsigned)count; }
            else if(op<6) { if(count<width)y=x>>(unsigned)count; }
            else {
                unsigned n=count<width?(unsigned)count:width-1;
                y=x>>n;
                if(n && (x&(UINT64_C(1)<<(width-1))))y|=UINT64_MAX<<(width-n);
            }
            memcpy(value+i,&y,bytes);
        }
    }
    memcpy(reg(cpu,dest),value,16);
}
void tb_sse_shiftimm(CPU,unsigned op,unsigned dest,unsigned count) {
    if(count>255)abort();legacy_shift_publish(cpu,op,dest,count);
}
void tb_sse_shiftcount(CPU,unsigned op,unsigned dest,unsigned source) {
    if(op>=8)abort();uint64_t count;memcpy(&count,reg(cpu,source),8);legacy_shift_publish(cpu,op,dest,count);
}
void tb_sse_shiftcountmem(CPU,unsigned op,unsigned dest,uint32_t address) {
    if(op>=8)abort();(void)reg(cpu,dest);
    unsigned char source[16];volatile const unsigned char *p=memory(cpu,address,TB_SSE_A128);
    for(unsigned i=0;i<16;++i)source[i]=p[i];
    uint64_t count;memcpy(&count,source,8);legacy_shift_publish(cpu,op,dest,count);
}
