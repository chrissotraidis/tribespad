#include "quadword_conversion.h"
#include <stddef.h>
int tb_quadword_conversion(unsigned op,uint64_t input,uint32_t control,
                           uint64_t *result,uint32_t *raised) {
    if(op>TB_QC_I64_PS || control>0xffff || !result || !raised)return 0;
    uint64_t value=0;unsigned flags=0;
    if(op<=TB_QC_PD_I64) {
        unsigned single=op<=TB_QC_PS_I64,signed_output=op&1;
        unsigned fraction_bits=single?23:52,exponent_bits=single?8:11,bias=single?127:1023;
        unsigned sign=(unsigned)((input>>(fraction_bits+exponent_bits))&1);
        uint64_t fraction=input&((UINT64_C(1)<<fraction_bits)-1);
        unsigned exponent=(unsigned)((input>>fraction_bits)&((1u<<exponent_bits)-1));
        int power=(int)exponent-(int)bias;
        uint64_t indefinite=signed_output?(UINT64_C(1)<<63):UINT64_MAX;
        if(!exponent && (!fraction || (control&0x40)))value=0;
        else if(exponent==((1u<<exponent_bits)-1) || power>63) {value=indefinite;flags=1;}
        else if(power<0)flags=32;
        else {
            uint64_t significand=(UINT64_C(1)<<fraction_bits)|fraction,remainder=0;
            if((unsigned)power>=fraction_bits)value=significand<<((unsigned)power-fraction_bits);
            else {
                unsigned shift=fraction_bits-(unsigned)power;
                value=significand>>shift;remainder=significand&((UINT64_C(1)<<shift)-1);
            }
            uint64_t limit=signed_output?((UINT64_C(1)<<63)-(sign?0:1)):UINT64_MAX;
            if(value>limit || (!signed_output && sign && value)) {value=indefinite;flags=1;}
            else {if(remainder)flags=32;if(sign)value=UINT64_C(0)-value;}
        }
    } else {
        unsigned single=op==TB_QC_I64_PS,sign=op!=TB_QC_U64_PD?(unsigned)(input>>63):0;
        unsigned fraction_bits=single?23:52,bias=single?127:1023;
        uint64_t magnitude=sign?UINT64_C(0)-input:input;
        if(magnitude) {
            unsigned power=0;for(uint64_t scan=magnitude;scan>>1;scan>>=1)++power;
            uint64_t significand,remainder=0;
            if(power<=fraction_bits)significand=magnitude<<(fraction_bits-power);
            else {
                unsigned shift=power-fraction_bits;significand=magnitude>>shift;
                remainder=magnitude&((UINT64_C(1)<<shift)-1);
                if(remainder) {
                    flags=32;unsigned round=(control>>13)&3;
                    uint64_t halfway=UINT64_C(1)<<(shift-1);
                    unsigned increment=round==0?(remainder>halfway || (remainder==halfway && (significand&1))):round==1?sign:round==2?!sign:0;
                    if(increment && ++significand==(UINT64_C(1)<<(fraction_bits+1))) {significand>>=1;++power;}
                }
            }
            value=((uint64_t)sign<<(single?31:63))|((uint64_t)(power+bias)<<fraction_bits)|
                  (significand&((UINT64_C(1)<<fraction_bits)-1));
        }
    }
    *result=value;*raised=flags;return 1;
}
