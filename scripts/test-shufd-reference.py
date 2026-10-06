#!/usr/bin/env python3
"""Canonical VPSHUFD against Python per-lane tuples for every imm8."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/shufd-reference';out.mkdir(parents=True,exist_ok=True);rng=random.Random(25034);requests=[];expected=[]
for mode in range(4):
 for i in range(8192):
  immediate=i//32;vectors=[[rng.getrandbits(32) for _ in range(8)] for _ in range(3)];source=2 if mode&1 else 1
  if i%32<16:vectors[source]=[0x01020304*(j+1) for j in range(8)]
  packed=[[row[j]|(row[j+1]<<32) for j in range(0,8,2)] for row in vectors];setting=i%16;sticky=[0,1,31,63][(i//16)%4];control=0x1f80|((setting&3)<<13)|(0x40 if setting&4 else 0)|(0x8000 if setting&8 else 0)|sticky;flags=[0,0x8d5,0x41,0x884][i%4];word=rng.getrandbits(32);requests.append(f'{mode} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for row in packed for v in row)+f' {immediate:x}\n');result=[]
  for lane in range(2 if mode>=2 else 1):result.extend(vectors[source][lane*4+(immediate//(4**j))%4] for j in range(4))
  if mode<2:result.extend([0]*4)
  want=[result[j]|(result[j+1]<<32) for j in range(0,8,2)];expected.append(want+[v for row in packed[1:] for v in row]+[word,control,flags]+packed[2])
request=''.join(requests);(out/'requests.txt').write_text(request);source=R/'tests/shufd_oracle.c';table=R/'tests/shufd_oracle.inc';exe=out/'oracle-x86';subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True);run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,check=True,timeout=120);(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(reply,want) in enumerate(zip(replies,expected)):assert [int(x,16) for x in reply.split()]==want,(i,reply,want)
r={'pass':True,'state':'finished','cases':len(expected),'modes':4,'source_sha256':sha(source),'table_sha256':sha(table),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'scope':'Canonical Rosetta VPSHUFD everyimm8 at bothwidths/REG/MEM versus per128-bit lane Python tuples, full3YMM/flags/maskedMXCSR/memory. Separate native corpus, not physicalIntel/fullDLLinit/gameplay proof.'};(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: %d canonical VPSHUFD reference cases'%len(expected))
