"""Exact rational signed-dword conversion; no host floating-point arithmetic."""
from f32_packed_reference import quantize
def convert(raw,control):
 value=raw if raw<1<<31 else raw-(1<<32);sign=int(value<0);magnitude=abs(value)
 if not magnitude:return 0,0
 exponent=magnitude.bit_length()-1
 whole,inexact=quantize(magnitude,1,0,exponent-52,(control>>13)&3,sign)
 assert not inexact
 return (sign<<63)|((exponent+1023)<<52)|(whole-(1<<52)),0
