"""Exact rational one-lane conversion oracle, independent of native bit math."""
import fractions
F=fractions.Fraction
def float_value(bits,single,control):
 n=23 if single else 52;e=8 if single else 11;bias=127 if single else 1023
 fraction=bits%2**n;exponent=bits//2**n%2**e;sign=bits//2**(n+e)%2
 if exponent==2**e-1:return None
 if exponent==0:
  if control&64:return F(0)
  value=F(fraction)*F(2)**(1-bias-n)
 else:value=F(2**n+fraction)*F(2)**(exponent-bias-n)
 return -value if sign else value

def oracle(op,bits,control):
 if op<4:
  value=float_value(bits,op<2,control);signed=op%2;indef=2**63 if signed else 2**64-1
  if value is None:return indef,1
  whole=int(value);low=-(2**63) if signed else 0;high=2**63-1 if signed else 2**64-1
  if not low<=whole<=high:return indef,1
  return whole%2**64,32 if F(whole)!=value else 0
 signed=op!=4;integer=bits-2**64 if signed and bits>=2**63 else bits
 if integer==0:return 0,0
 sign=integer<0;mag=abs(integer);precision=24 if op==6 else 53;power=mag.bit_length()-1;unit=F(2)**(power-precision+1)
 lower=F(mag)//unit;remainder=F(mag)-lower*unit;mode=control//8192%4
 increment=bool(remainder) and (mode==0 and (remainder>unit/2 or remainder==unit/2 and lower%2) or mode==1 and sign or mode==2 and not sign)
 rounded=(lower+increment)*unit
 exponent=power
 if rounded>=2**(power+1):exponent+=1
 n=precision-1;significand=int(rounded/F(2)**(exponent-n));bias=127 if op==6 else 1023
 return (int(sign)*2**(31 if op==6 else 63)+(exponent+bias)*2**n+significand-2**n),32 if remainder else 0

