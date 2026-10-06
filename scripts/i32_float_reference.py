"""Exact rational signed-dword conversion; no host floating-point arithmetic."""
from f32_packed_reference import quantize
def convert(raw,control):
 value=raw if raw<1<<31 else raw-(1<<32);sign=int(value<0);magnitude=abs(value)
 if not magnitude:return 0,0
 exponent=magnitude.bit_length()-1
 whole,inexact=quantize(magnitude,1,0,exponent-23,(control>>13)&3,sign)
 if whole==1<<24:whole>>=1;exponent+=1
 return (sign<<31)|((exponent+127)<<23)|(whole-(1<<23)),32 if inexact else 0
