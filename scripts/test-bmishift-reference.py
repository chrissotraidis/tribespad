#!/usr/bin/env python3
"""Compare canonical BMI2 32-bit shifts with independent Python arithmetic."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/bmishift-reference';out.mkdir(parents=True,exist_ok=True);rng=random.Random(25034);requests=[];expected=[]
for mode in range(8):
 for i in range(4096):
  count=i//16;a=rng.getrandbits(64);b=(rng.getrandbits(64)&~0xffffffff)|count;c=rng.getrandbits(64);word=rng.getrandbits(32)
  if i%16<8:a=(a&~0xffffffff)|[0,1,0x80000000,0xffffffff,0x7fffffff,0x55555555,0xaaaaaaaa,0x01234567][i%16]
  if i%16>=8:b=(b&~0xffffffff)|((rng.getrandbits(32)&~31)|(count%32))
  flags=[0,0x8d5,0x41,0x884][i%4];requests.append(f'{mode} {flags:x} {a:016x} {b:016x} {c:016x} {word:x}\n');source=word if mode%4==1 else a&0xffffffff;factor=2**(b%32);value=(source//factor if mode>=4 else source*factor)&0xffffffff;after=[c,a,b];after[[0,0,1,2][mode%4]]=value;expected.append(after+[word,flags])
(out/'requests.txt').write_text(''.join(requests));source=R/'tests/bmishift_oracle.c';exe=out/'oracle-x86';subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True);run=subprocess.run(['arch','-x86_64',str(exe)],input=''.join(requests),text=True,capture_output=True,check=True,timeout=120);(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(reply,want) in enumerate(zip(replies,expected)):assert [int(x,16) for x in reply.split()]==want,(i,reply,want)
r={'pass':True,'state':'finished','cases':len(expected),'modes':8,'source_sha256':sha(source),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'scope':'Canonical Rosetta x86-64 W0 SHLX/SHRX with 32-bit operands REG/MEM/destsourcealias/destcountalias, low5 countbits/highbitcounts/all32countvalues, independent Python arithmetic, full3GPR/flags/guardedunalignedmemory. IA32 WIG checked in decoder/native corpus, not x86-64 W1. Not physicalIntel/fullinitialization/gameplay proof.'};(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: %d canonical BMI2 shift cases'%len(expected))
