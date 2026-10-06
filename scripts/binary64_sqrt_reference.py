"""Exact integer square-root and binary64 rounding model."""
import math
def root(source,control):
 sign=source>>63;mag=source&0x7fffffffffffffff;frac=source&0x000fffffffffffff;exponent=(source>>52)&2047
 if exponent==2047 and frac:return source|0x0008000000000000,0 if frac&0x0008000000000000 else 1
 if exponent==0 and frac and control&0x40:return sign<<63,0
 if sign and mag:return 0xfff8000000000000,1
 if not mag or exponent==2047:return source,0
 mantissa=frac|(1<<52) if exponent else frac
 power=exponent-1023-52 if exponent else -1074
 result_exponent=(mantissa.bit_length()-1+power)//2
 shift=power-2*result_exponent+104;assert shift>=0
 radicand=mantissa<<shift;value=math.isqrt(radicand);inexact=value*value!=radicand
 mode=(control>>13)&3
 if mode==2 and inexact or mode==0 and 4*radicand>(2*value+1)**2:value+=1
 if value==1<<53:value>>=1;result_exponent+=1
 assert 1<<52<=value<1<<53 and -1022<=result_exponent<=1023
 return ((result_exponent+1023)<<52)|(value-(1<<52)),(2 if not exponent else 0)|(32 if inexact else 0)
