#!/usr/bin/env python3
"""Check scalar narrowing with exact integer quantization and IEEE boundaries."""
import argparse,hashlib,json,pathlib
from itertools import zip_longest
ap=argparse.ArgumentParser();ap.add_argument('directory',type=pathlib.Path);a=ap.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def quantize(mantissa,power,step,mode,sign):
 shift=power-step
 if shift>=0:return mantissa<<shift,False
 denominator=1<<-shift;whole,remainder=divmod(mantissa,denominator)
 if mode==0 and (2*remainder>denominator or 2*remainder==denominator and whole&1) or remainder and (mode==1 and sign or mode==2 and not sign):whole+=1
 return whole,bool(remainder)
def narrow(source,control):
 sign=source>>63;out_sign=sign<<31;exponent=(source>>52)&2047;fraction=source&0x000fffffffffffff
 if exponent==2047:
  if not fraction:return out_sign|0x7f800000,0
  return out_sign|0x7fc00000|(fraction>>29),0 if fraction&0x0008000000000000 else 1
 if not exponent and (not fraction or control&0x40):return out_sign,0
 status=2 if not exponent else 0
 mantissa=fraction|(1<<52) if exponent else fraction
 power=exponent-1023-52 if exponent else -1074
 normal_exponent=mantissa.bit_length()-1+power;mode=(control>>13)&3
 working,_=quantize(mantissa,power,normal_exponent-23,mode,sign)
 working_exponent=normal_exponent+(working==1<<24)
 if working_exponent>=128:
  infinity=mode==0 or mode==1 and sign or mode==2 and not sign
  return out_sign|(0x7f800000 if infinity else 0x7f7fffff),status|40
 tiny=working_exponent < -126
 whole,inexact=quantize(mantissa,power,max(normal_exponent-23,-149),mode,sign)
 if inexact:status|=32
 if tiny and control&0x8000:return out_sign,status|48
 if tiny and inexact:status|=16
 if normal_exponent < -126:return out_sign|whole,status
 if whole==1<<24:whole>>=1;normal_exponent+=1
 return out_sign|((normal_exponent+127)<<23)|(whole-(1<<23)),status
count=0
with (a.directory/'requests.txt').open() as requests,(a.directory/'reference.txt').open() as replies:
 for request,reply in zip_longest(requests,replies):
  assert request is not None and reply is not None
  q=request.split();r=reply.split()
  if int(q[0])!=10:continue
  control=int(q[1],16);source=int(q[12],16);value,status=narrow(source,control)
  assert int(r[0],16)&0xffffffff==value,(count,q,r,hex(value))
  assert int(r[13],16)==control|status,(count,q,r,status)
  assert int(r[14],16)==int(q[2],16)&0x8d5
  count+=1
assert count>0
report={'pass':True,'cases':count,'request_sha256':sha(a.directory/'requests.txt'),'reference_sha256':sha(a.directory/'reference.txt'),'source_sha256':sha(pathlib.Path(__file__)),'scope':'Independent exact integer quantization of Rosetta VCVTSD2SS low result and masked status, including unbounded working precision tininess; native/full vector checks are separate'}
(a.directory/'narrowing-reference-proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
