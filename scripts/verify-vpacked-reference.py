#!/usr/bin/env python3
"""Verify packed binary32 results/status with exact integer arithmetic."""
import argparse,hashlib,json,pathlib,struct
from itertools import zip_longest
from f32_packed_reference import multiply,subtract,add,divide
ap=argparse.ArgumentParser();ap.add_argument('directory',type=pathlib.Path);a=ap.parse_args()
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
counts={str(mode):0 for mode in range(60,76)}
with (a.directory/'requests.txt').open() as requests,(a.directory/'reference.txt').open() as replies:
 for request,reply in zip_longest(requests,replies):
  assert request is not None and reply is not None
  q=request.split();r=reply.split();mode=int(q[0])
  if not 60<=mode<=75:continue
  assert len(q)==17 and len(r)==19 and int(q[16],16)==0
  before=struct.pack('<12Q',*(int(v,16) for v in q[4:16]));after=struct.pack('<12Q',*(int(v,16) for v in r[:12]));control=int(q[1],16)
  wide=bool(mode&1);left=struct.unpack_from('<8I',before,32);right=struct.unpack_from('<8I',before,64);output=[];status=0
  for lane in range(8 if wide else 4):
   value,raised=(divide if mode>=72 else add if mode>=68 else subtract if mode>=64 else multiply)(left[lane],right[lane],control);output.append(value);status|=raised
  wanted=bytearray(before);wanted[:32]=struct.pack('<'+str(len(output))+'I',*output)+(b'' if wide else bytes(16))
  assert after==wanted,(counts,q,r,output)
  assert int(r[12],16)==int(q[3],16) and int(r[13],16)==control|status,(counts,q,r,status)
  assert int(r[14],16)==int(q[2],16)&0x8d5
  assert struct.pack('<4Q',*(int(v,16) for v in r[15:19]))==before[64:96]
  counts[str(mode)]+=1
assert all(counts.values())
report={'pass':True,'cases':sum(counts.values()),'cases_per_mode':counts,'request_sha256':sha(a.directory/'requests.txt'),'reference_sha256':sha(a.directory/'reference.txt'),'source_sha256':sha(pathlib.Path(__file__)),'model_sha256':sha(pathlib.Path(__file__).with_name('f32_packed_reference.py')),'scope':'Independent exact integer multiplication/subtraction/addition/division/quantization of every packed reference lane, aggregate masked MXCSR status, NaN/DAZ/FTZ/rounding and preserved source/state. Native full CPU/memory checked separately; no precise unmasked Windows fault priority or physical Intel claim.'}
(a.directory/'packed-reference-proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
