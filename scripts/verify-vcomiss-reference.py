#!/usr/bin/env python3
"""Independently classify ordered binary32 comparison without host floats."""
import argparse,hashlib,json,pathlib
from itertools import zip_longest
ap=argparse.ArgumentParser();ap.add_argument('directory',type=pathlib.Path);a=ap.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
count=0
with (a.directory/'requests.txt').open() as requests,(a.directory/'reference.txt').open() as replies:
 for request,reply in zip_longest(requests,replies):
  assert request is not None and reply is not None
  q=request.split();r=reply.split()
  if int(q[0])!=11:continue
  control=int(q[1],16);operands=[int(q[i],16)&0xffffffff for i in [8,12]];status=0
  nan=[(v&0x7f800000)==0x7f800000 and bool(v&0x007fffff) for v in operands]
  if any(nan):flags=0x45;status=1
  else:
   for i,v in enumerate(operands):
    if not v&0x7f800000 and v&0x007fffff:
     if control&0x40:operands[i]=v&0x80000000
     else:status=2
   left,right=operands;lm=left&0x7fffffff;rm=right&0x7fffffff
   if left==right or not(lm or rm):flags=0x40
   else:
    ls=left>>31;rs=right>>31
    less=ls>rs if ls!=rs else lm>rm if ls else lm<rm
    flags=1 if less else 0
  assert int(r[14],16)==flags,(count,q,r,flags)
  assert int(r[13],16)==control|status,(count,q,r,status)
  count+=1
assert count>0
report={'pass':True,'cases':count,'request_sha256':sha(a.directory/'requests.txt'),'reference_sha256':sha(a.directory/'reference.txt'),'source_sha256':sha(pathlib.Path(__file__)),'scope':'Independent integer classification of Rosetta VCOMISS flags and masked MXCSR status; native/full vector checks are in the emitted suite'}
(a.directory/'ordered-comparison-reference-proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
