#!/usr/bin/env python3
"""Independently verify high/low quadword selection and preserved reference state."""
import argparse,hashlib,json,pathlib,struct
from itertools import zip_longest
ap=argparse.ArgumentParser();ap.add_argument('directory',type=pathlib.Path);a=ap.parse_args()
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
counts={str(mode):0 for mode in range(54,60)}
with (a.directory/'requests.txt').open() as requests,(a.directory/'reference.txt').open() as replies:
 for request,reply in zip_longest(requests,replies):
  assert request is not None and reply is not None
  q=request.split();r=reply.split();mode=int(q[0])
  if not 54<=mode<=59:continue
  assert len(q)==17 and len(r)==19 and int(q[16],16)==0
  before=[int(v,16) for v in q[4:16]];after=[int(v,16) for v in r[:12]];wanted=before[:]
  if mode==54:wanted[:4]=[before[9],before[5],0,0]
  elif mode in (55,57):wanted[:4]=[before[4],before[8],0,0]
  elif mode==56:wanted[:4]=[before[8],before[5],0,0]
  assert after==wanted,(counts,q,r)
  assert int(r[12],16)==int(q[3],16) and int(r[13],16)==int(q[1],16)
  assert int(r[14],16)==int(q[2],16)&0x8d5
  packed=[int(v,16) for v in r[15:19]];expected=before[8:12]
  if mode in (58,59):expected[0]=before[8+(mode==59)]
  assert packed==expected,(counts,q,r)
  counts[str(mode)]+=1
assert all(counts.values())
report={'pass':True,'cases':sum(counts.values()),'cases_per_mode':counts,'request_sha256':sha(a.directory/'requests.txt'),'reference_sha256':sha(a.directory/'reference.txt'),'source_sha256':sha(pathlib.Path(__file__)),'scope':'Independent quadword model of high/low register merging, eight-byte memory loads/stores, cleared register upper halves and unchanged remaining vectors/MXCSR/flags/memory; full native CPU/guards checked separately; software reference, not physical Intel proof'}
(a.directory/'half-reference-proof.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
