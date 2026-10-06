#!/usr/bin/env python3
"""Canonical integer insertion versus Python half-vector tuples."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/inserti-reference';out.mkdir(parents=True,exist_ok=True);rng=random.Random(25034);requests=[];expected=[]
for mode in range(3):
 for i in range(8192):
  immediate=i//32;vectors=[[rng.getrandbits(64) for _ in range(4)] for _ in range(3)]
  if i%32<16:vectors[1]=[0x0102030405060708*(j+1) for j in range(4)]
  setting=i%16;sticky=[0,1,31,63][(i//16)%4];control=0x1f80|((setting&3)<<13)|(0x40 if setting&4 else 0)|(0x8000 if setting&8 else 0)|sticky;flags=[0,0x8d5,0x41,0x884][i%4];word=rng.getrandbits(32);requests.append(f'{mode} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for row in vectors for v in row)+f' {immediate:x}\n');after=[row[:] for row in vectors];memory=vectors[2][:];result=vectors[1][:];half=immediate%2
  result[half*2:half*2+2]=vectors[2][:2];after[1 if mode==2 else 0]=result
  expected.append([v for row in after for v in row]+[word,control,flags]+memory)

request=''.join(requests);(out/'requests.txt').write_text(request);source=R/'tests/inserti_oracle.c';table=R/'tests/inserti_oracle.inc';exe=out/'oracle-x86';subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True);run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,check=True,timeout=120);(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(reply,want) in enumerate(zip(replies,expected)):assert [int(x,16) for x in reply.split()]==want,(i,reply,want)
r={'pass':True,'state':'finished','cases':len(expected),'modes':3,'source_sha256':sha(source),'table_sha256':sha(table),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'scope':'Canonical Rosetta VINSERTI128 everyimm8 REG/MEM/aliasedREG versus Python halfvectors, halfpreservation/exact16-byte reads/sourcepreservation/full3YMM/flags/maskedMXCSR/memory. Separate native corpus, not physicalIntel/fullDLLinit/gameplay proof.'};(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: %d canonical integer insertion reference cases'%len(expected))
