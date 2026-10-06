#!/usr/bin/env python3
"""Verify VEX binary64 square roots using exact Python integer arithmetic."""
import argparse,hashlib,json,math,pathlib
from itertools import zip_longest
ap=argparse.ArgumentParser();ap.add_argument('directory',type=pathlib.Path);a=ap.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
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
count=0
with (a.directory/'requests.txt').open() as requests,(a.directory/'reference.txt').open() as replies:
 for request,reply in zip_longest(requests,replies):
  assert request is not None and reply is not None
  q=request.split();r=reply.split()
  if int(q[0])!=9:continue
  control=int(q[1],16);source=int(q[12],16);value,status=root(source,control)
  assert int(r[0],16)==value,(count,q,r,hex(value))
  assert int(r[13],16)==control|status,(count,q,r,status)
  assert int(r[14],16)==int(q[2],16)&0x8d5
  count+=1
assert count>0
report={'pass':True,'cases':count,'request_sha256':sha(a.directory/'requests.txt'),'reference_sha256':sha(a.directory/'reference.txt'),'source_sha256':sha(pathlib.Path(__file__)),'scope':'Exact integer-square-root/rounding classification of Rosetta VSQRTSD low result and masked MXCSR status; native/full vector checks are in the emitted suite'}
(a.directory/'square-root-reference-proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
