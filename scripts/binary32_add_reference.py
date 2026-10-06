"""Exact integer binary32 addition model; no host floating-point arithmetic."""
def quantize(numerator,denominator,power,step,mode,sign):
 shift=power-step
 if shift>=0:numerator<<=shift
 else:denominator<<=-shift
 whole,remainder=divmod(numerator,denominator)
 if mode==0 and (2*remainder>denominator or 2*remainder==denominator and whole&1) or remainder and (mode==1 and sign or mode==2 and not sign):whole+=1
 return whole,bool(remainder)
def subtract(left,right,control):
 operands=[left,right];nan=[(v&0x7fffffff)>0x7f800000 for v in operands]
 if any(nan):return (left if nan[0] else right)|0x00400000,1 if any(n and not v&0x00400000 for n,v in zip(nan,operands)) else 0
 status=0
 for i,v in enumerate(operands):
  if not v&0x7f800000 and v&0x007fffff:
   if control&0x40:operands[i]=v&0x80000000
   else:status=2
 left,right=operands;lm=left&0x7fffffff;rm=right&0x7fffffff;ls=left>>31;rs=right>>31;mode=control>>13&3
 if lm==rm==0x7f800000 and ls==rs:return 0xffc00000,1
 if lm==0x7f800000:return left,status
 if rm==0x7f800000:return right^0x80000000,status
 le=left>>23&255;re=right>>23&255
 ma=(left&0x007fffff)|(1<<23) if le else left&0x007fffff
 mb=(right&0x007fffff)|(1<<23) if re else right&0x007fffff
 pa=le-150 if le else -149;pb=re-150 if re else -149;power=min(pa,pb)
 signed=(-ma if ls else ma)*(1<<(pa-power))-(-mb if rs else mb)*(1<<(pb-power))
 if not signed:return (ls if ls==(rs^1) else int(mode==1))<<31,status
 sign=int(signed<0);out_sign=sign<<31;numerator=abs(signed);exponent=numerator.bit_length()-1+power
 working,_=quantize(numerator,1,power,exponent-23,mode,sign)
 working_exponent=exponent+(working==1<<24)
 if working_exponent>=128:
  infinity=mode==0 or mode==1 and sign or mode==2 and not sign
  return out_sign|(0x7f800000 if infinity else 0x7f7fffff),status|40
 whole,inexact=quantize(numerator,1,power,max(exponent-23,-149),mode,sign)
 if inexact:status|=32
 tiny=working_exponent < -126
 if tiny and control&0x8000:return out_sign,status|48
 if tiny and inexact:status|=16
 if exponent < -126:return out_sign|whole,status
 if whole==1<<24:whole>>=1;exponent+=1
 return out_sign|((exponent+127)<<23)|(whole-(1<<23)),status

def add(left,right,control):
 nan=[(v&0x7fffffff)>0x7f800000 for v in [left,right]]
 if any(nan):return (left if nan[0] else right)|0x00400000,1 if any(n and not v&0x00400000 for n,v in zip(nan,[left,right])) else 0
 return subtract(left,right^0x80000000,control)
