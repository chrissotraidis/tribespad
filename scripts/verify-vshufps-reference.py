#!/usr/bin/env python3
"""Independently check VSHUFPS byte-lane selection and preserved reference state."""
import argparse,hashlib,json,pathlib,struct
from itertools import zip_longest
ap=argparse.ArgumentParser();ap.add_argument('directory',type=pathlib.Path);a=ap.parse_args()
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
count=0;controls={str(mode):set() for mode in range(48,52)}
with (a.directory/'requests.txt').open() as requests,(a.directory/'reference.txt').open() as replies:
 for request,reply in zip_longest(requests,replies):
  assert request is not None and reply is not None
  q=request.split();r=reply.split();mode=int(q[0])
  if not 48<=mode<=51:continue
  assert mode<=51 and len(q)==17 and len(r)==19
  control=int(q[16],16);controls[str(mode)].add(control)
  before=struct.pack('<12Q',*(int(v,16) for v in q[4:16]));after=struct.pack('<12Q',*(int(v,16) for v in r[:12]));wanted=bytearray(before)
  result=[]
  for group in range(2 if mode in (49,51) else 1):
   left=struct.unpack_from('<4I',before,32+group*16);right=struct.unpack_from('<4I',before,64+group*16)
   result.extend([left[control&3],left[(control>>2)&3],right[(control>>4)&3],right[(control>>6)&3]])
  wanted[:32]=struct.pack('<'+str(len(result))+'I',*result)+(bytes(16) if len(result)==4 else b'')
  assert after==wanted,(count,q,r)
  assert int(r[12],16)==int(q[3],16) and int(r[13],16)==int(q[1],16)
  assert int(r[14],16)==int(q[2],16)&0x8d5
  assert struct.pack('<4Q',*(int(v,16) for v in r[15:19]))==before[64:96]
  count+=1
assert count>0 and all(v==set(range(256)) for v in controls.values())
report={'pass':True,'cases':count,'controls_per_mode':{k:len(v) for k,v in controls.items()},'request_sha256':sha(a.directory/'requests.txt'),'reference_sha256':sha(a.directory/'reference.txt'),'source_sha256':sha(pathlib.Path(__file__)),'scope':'Independent integer-lane model of all VSHUFPS reference vectors and unchanged memory/MXCSR/flags, both widths and register/memory references; full native CPU and guarded-memory checks are separate; not physical Intel proof'}
(a.directory/'shuffle-reference-proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
