"""Exact integer reference arithmetic; no host floating-point computation."""
def quantize(numerator,denominator,power,step,mode,sign):
 shift=power-step
 if shift>=0:numerator<<=shift
 else:denominator<<=-shift
 whole,remainder=divmod(numerator,denominator)
 if mode==0 and (2*remainder>denominator or 2*remainder==denominator and whole&1) or remainder and (mode==1 and sign or mode==2 and not sign):whole+=1
 return whole,bool(remainder)
def divide(left,right,control):
 operands=[left,right];nan=[(v&0x7fffffffffffffff)>0x7ff0000000000000 for v in operands]
 if any(nan):return (left if nan[0] else right)|0x0008000000000000,1 if any(n and not v&0x0008000000000000 for n,v in zip(nan,operands)) else 0
 status=0
 for i,v in enumerate(operands):
  if not v&0x7ff0000000000000 and v&0x000fffffffffffff:
   if control&0x40:operands[i]=v&0x8000000000000000
   else:status=2
 left,right=operands;lm=left&0x7fffffffffffffff;rm=right&0x7fffffffffffffff;sign=(left^right)>>63;out_sign=sign<<63
 if (not lm and not rm) or (lm==0x7ff0000000000000 and rm==0x7ff0000000000000):return 0xfff8000000000000,1
 if lm==0x7ff0000000000000:return out_sign|0x7ff0000000000000,status
 if rm==0x7ff0000000000000:return out_sign,status
 if not rm:return out_sign|0x7ff0000000000000,4
 if not lm:return out_sign,status
 le=left>>52&2047;re=right>>52&2047
 ma=(left&0x000fffffffffffff)|(1<<52) if le else left&0x000fffffffffffff
 mb=(right&0x000fffffffffffff)|(1<<52) if re else right&0x000fffffffffffff
 numerator=ma;denominator=mb;power=(le-1075 if le else -1074)-(re-1075 if re else -1074)
 ratio_exponent=numerator.bit_length()-denominator.bit_length()
 if ratio_exponent>=0:
  if numerator<(denominator<<ratio_exponent):ratio_exponent-=1
 elif (numerator<<-ratio_exponent)<denominator:ratio_exponent-=1
 exponent=ratio_exponent+power;mode=control>>13&3
 working,_=quantize(numerator,denominator,power,exponent-52,mode,sign)
 working_exponent=exponent+(working==1<<53)
 if working_exponent>=1024:
  infinity=mode==0 or mode==1 and sign or mode==2 and not sign
  return out_sign|(0x7ff0000000000000 if infinity else 0x7fefffffffffffff),status|40
 whole,inexact=quantize(numerator,denominator,power,max(exponent-52,-1074),mode,sign)
 if inexact:status|=32
 tiny=working_exponent < -1022
 if tiny and control&0x8000:return out_sign,status|48
 if tiny and inexact:status|=16
 if exponent < -1022:return out_sign|whole,status
 if whole==1<<53:whole>>=1;exponent+=1
 return out_sign|((exponent+1023)<<52)|(whole-(1<<52)),status
