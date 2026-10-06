#!/usr/bin/env python3
"""Independently verify half selection, register clearing and exact stores."""
import argparse,hashlib,json,pathlib,struct
from itertools import zip_longest
ap=argparse.ArgumentParser();ap.add_argument('directory',type=pathlib.Path);a=ap.parse_args()
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
count=0;controls={str(mode):set() for mode in (52,53)}
with (a.directory/'requests.txt').open() as requests,(a.directory/'reference.txt').open() as replies:
 for request,reply in zip_longest(requests,replies):
  assert request is not None and reply is not None
  q=request.split();r=reply.split();mode=int(q[0])
  if mode not in (52,53):continue
  assert len(q)==17 and len(r)==19
  control=int(q[16],16);assert 0<=control<=255;controls[str(mode)].add(control)
  before=struct.pack('<12Q',*(int(v,16) for v in q[4:16]));after=struct.pack('<12Q',*(int(v,16) for v in r[:12]))
  source=struct.unpack_from('<4Q',before,64)
  half=struct.pack('<2Q',*source[2*(control&1):2*(control&1)+2])
  wanted=bytearray(before)
  if mode==52:wanted[:32]=half+bytes(16)
  assert after==wanted,(count,q,r)
  assert int(r[12],16)==int(q[3],16) and int(r[13],16)==int(q[1],16)
  assert int(r[14],16)==int(q[2],16)&0x8d5
  packed=struct.pack('<4Q',*(int(v,16) for v in r[15:19]))
  assert packed==(half+before[80:96] if mode==53 else before[64:96])
  count+=1
assert count>0 and all(v==set(range(256)) for v in controls.values())
report={'pass':True,'cases':count,'controls_per_mode':{k:len(v) for k,v in controls.items()},'request_sha256':sha(a.directory/'requests.txt'),'reference_sha256':sha(a.directory/'reference.txt'),'source_sha256':sha(pathlib.Path(__file__)),'scope':'Independent integer-half model of every extraction reference, all immediate values, register upper clearing, sixteen-byte store and unchanged remaining vectors/MXCSR/flags/memory; native full CPU and guards checked separately; not physical Intel proof'}
(a.directory/'extraction-reference-proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
