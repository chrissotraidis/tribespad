#!/usr/bin/env python3
"""Canonical and two original XOR/OR instructions versus Python wide integers."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/sse/vinteger-logic-reference';out.mkdir(parents=True,exist_ok=True);rng=random.Random(25034);requests=[];expected=[]
for mode in range(10):
 for i in range(1024):
  vectors=[[rng.getrandbits(64) for _ in range(4)] for _ in range(3)]
  if i<32:
   for reg in range(3):
    for lane in range(4):vectors[reg][lane]=[0,0xffffffffffffffff,1<<((i+lane*17+reg*7)%64),0x7f81234500000001,0x8000000180000000,0x7ff012345678abcd,0x55555555aaaaaaaa,0xaaaaaaaa55555555][(i+reg+lane)%8]
  setting=i%16;sticky=[0,1,31,63][(i//16)%4];control=0x1f80|((setting&3)<<13)|(0x40 if setting&4 else 0)|(0x8000 if setting&8 else 0)|sticky;flags=[0,0x8d5,0x41,0x884][i%4];word=rng.getrandbits(32)
  requests.append(f'{mode} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for row in vectors for v in row)+' 0\n')
  output=[row[:] for row in vectors];wide=mode<8 and mode%4>=2;bits=256 if wide else 128
  a,b=(vectors[0],vectors[0]) if mode==8 else (vectors[0],vectors[1]) if mode==9 else (vectors[1],vectors[2])
  left=sum(v<<(64*j) for j,v in enumerate(a));right=sum(v<<(64*j) for j,v in enumerate(b));value=(left|right) if mode in [4,5,6,7,9] else left^right;value&=(1<<bits)-1
  output[0]=[(value>>(64*j))&((1<<64)-1) for j in range(4)]
  expected.append([v for row in output for v in row]+[word,control,flags]+vectors[2])
request=''.join(requests);(out/'requests.txt').write_text(request);source=R/'tests/vinteger_logic_oracle.c';exe=out/'oracle-x86'
subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True)
run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,check=True,timeout=120);(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(reply,want) in enumerate(zip(replies,expected)):assert [int(x,16) for x in reply.split()]==want,(i,reply,want)
r={'pass':True,'state':'finished','cases':len(expected),'modes':10,'source_sha256':sha(source),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'scope':'Direct canonical 128/256 REG/MEM plus original self-XOR/original OR bytes through Rosetta versus Python wide-integer bit operations; full3YMM/MXCSR/selectedflags/memory. Separate corpus from native alias cases; not physical Intel/unmasked x86 or full DLL/gameplay proof.'}
(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
