#!/usr/bin/env python3
"""Check masked VDIVSS using exact rational division and integer rounding."""
import argparse,hashlib,json,pathlib
from itertools import zip_longest
ap=argparse.ArgumentParser();ap.add_argument('directory',type=pathlib.Path);a=ap.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def quantize(numerator,denominator,power,step,mode,sign):
 shift=power-step
 if shift>=0:numerator<<=shift
 else:denominator<<=-shift
 whole,remainder=divmod(numerator,denominator)
 if mode==0 and (2*remainder>denominator or 2*remainder==denominator and whole&1) or remainder and (mode==1 and sign or mode==2 and not sign):whole+=1
 return whole,bool(remainder)
def divide(left,right,control):
 operands=[left,right];nan=[(v&0x7fffffff)>0x7f800000 for v in operands]
 if any(nan):return (left if nan[0] else right)|0x00400000,1 if any(n and not v&0x00400000 for n,v in zip(nan,operands)) else 0
 status=0
 for i,v in enumerate(operands):
  if not v&0x7f800000 and v&0x007fffff:
   if control&0x40:operands[i]=v&0x80000000
   else:status=2
 left,right=operands;lm=left&0x7fffffff;rm=right&0x7fffffff;sign=(left^right)>>31;out_sign=sign<<31
 if (not lm and not rm) or lm==rm==0x7f800000:return 0xffc00000,1
 if not rm and lm<0x7f800000:return out_sign|0x7f800000,4
 if lm==0x7f800000:return out_sign|0x7f800000,status
 if not lm or rm==0x7f800000:return out_sign,status
 le=left>>23&255;re=right>>23&255
 numerator=(left&0x007fffff)|(1<<23) if le else left&0x007fffff
 denominator=(right&0x007fffff)|(1<<23) if re else right&0x007fffff
 power=(le-150 if le else -149)-(re-150 if re else -149)
 ratio_exponent=numerator.bit_length()-denominator.bit_length()
 if (numerator < denominator<<ratio_exponent) if ratio_exponent>=0 else (numerator<<-ratio_exponent < denominator):ratio_exponent-=1
 exponent=ratio_exponent+power;mode=control>>13&3
 working,_=quantize(numerator,denominator,power,exponent-23,mode,sign)
 working_exponent=exponent+(working==1<<24)
 if working_exponent>=128:
  infinity=mode==0 or mode==1 and sign or mode==2 and not sign
  return out_sign|(0x7f800000 if infinity else 0x7f7fffff),status|40
 whole,inexact=quantize(numerator,denominator,power,max(exponent-23,-149),mode,sign)
 if inexact:status|=32
 tiny=working_exponent < -126
 if tiny and control&0x8000:return out_sign,status|48
 if tiny and inexact:status|=16
 if exponent < -126:return out_sign|whole,status
 if whole==1<<24:whole>>=1;exponent+=1
 return out_sign|((exponent+127)<<23)|(whole-(1<<23)),status
count=0
with (a.directory/'requests.txt').open() as requests,(a.directory/'reference.txt').open() as replies:
 for request,reply in zip_longest(requests,replies):
  assert request is not None and reply is not None
  q=request.split();r=reply.split()
  if int(q[0])!=13:continue
  control=int(q[1],16);left=int(q[8],16)&0xffffffff;right=int(q[12],16)&0xffffffff;value,status=divide(left,right,control)
  assert int(r[0],16)&0xffffffff==value,(count,q,r,hex(value))
  assert int(r[13],16)==control|status,(count,q,r,status)
  assert int(r[14],16)==int(q[2],16)&0x8d5
  count+=1
assert count>0
report={'pass':True,'cases':count,'request_sha256':sha(a.directory/'requests.txt'),'reference_sha256':sha(a.directory/'reference.txt'),'source_sha256':sha(pathlib.Path(__file__)),'scope':'Independent exact rational/integer quantization of Rosetta VDIVSS low result and masked MXCSR status, including NaN/DAZ/precomputation/tininess/FTZ; native/full vector checks are separate'}
(a.directory/'division-reference-proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
