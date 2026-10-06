#!/usr/bin/env python3
"""Canonical high-qword interleave versus Python tuples."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/unpackhighq-reference';out.mkdir(parents=True,exist_ok=True);rng=random.Random(25034);requests=[];expected=[]
for mode in range(4):
 for i in range(8192):
  vectors=[[rng.getrandbits(64) for _ in range(4)] for _ in range(3)]
  if i<256:vectors[1]=[0x0101010101010101*((i+j*17)&255) for j in range(4)];vectors[2]=[0x0101010101010101*((255-i-j*31)&255) for j in range(4)]
  setting=i%16;sticky=[0,1,31,63][(i//16)%4];control=0x1f80|((setting&3)<<13)|(0x40 if setting&4 else 0)|(0x8000 if setting&8 else 0)|sticky;flags=[0,0x8d5,0x41,0x884][i%4];word=rng.getrandbits(32);requests.append(f'{mode} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for row in vectors for v in row)+' 0\n');value=[vectors[1][1],vectors[2][1],vectors[1][3] if mode>=2 else 0,vectors[2][3] if mode>=2 else 0];expected.append(value+[v for row in vectors[1:] for v in row]+[word,control,flags]+vectors[2])
request=''.join(requests);(out/'requests.txt').write_text(request);source=R/'tests/unpackhighq_oracle.c';exe=out/'oracle-x86';subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True);run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,check=True,timeout=120);(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(reply,want) in enumerate(zip(replies,expected)):assert [int(x,16) for x in reply.split()]==want,(i,reply,want)
r={'pass':True,'state':'finished','cases':len(expected),'modes':4,'source_sha256':sha(source),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'scope':'Rosetta canonical VPUNPCKHQDQ 128/256 REG/MEM versus Python quadword tuple expectations, lowdata ignored, per128-bit lane, full3YMM/flags/maskedMXCSR/memory. Separate native corpus, not physicalIntel/fullDLL initialization/gameplay proof.'};(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: %d canonical high-qword unpack reference cases'%len(expected))
