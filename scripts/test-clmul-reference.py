#!/usr/bin/env python3
"""Check 128-bit x86 carryless multiplication under Rosetta against Python GF(2)."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/sse/clmul-reference';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
rng=random.Random(0x6c434d55);specs=[]
edge=[0,1,2,1<<63,(1<<64)-1,0xaaaaaaaaaaaaaaaa,0x5555555555555555,0xffffffff00000000]
for control in [0,1,16,17]:
 for a in edge:
  for b in edge:specs.append((control,[a,a^((1<<64)-1),b,b^((1<<64)-1)]))
 for i in range(64):
  for j in range(64):specs.append((control,[1<<i,1<<i,1<<j,1<<j]))
 for _ in range(4096):specs.append((control,[rng.getrandbits(64) for _ in range(4)]))
requests=''.join(str(c)+' '+ ' '.join('%016x'%v for v in words)+'\n' for c,words in specs)
(out/'requests.txt').write_text(requests)
source=R/'tests/clmul_oracle.c';exe=out/'reference';sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
cmd=['clang','-target','x86_64-apple-macos12','-isysroot',sdk,'-O2','-mavx','-mpclmul','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)]
subprocess.run(cmd,check=True);run=subprocess.run([str(exe)],input=requests,capture_output=True,text=True,timeout=60)
assert run.returncode==0 and not run.stderr,(run.returncode,run.stderr)
lines=run.stdout.splitlines();assert len(lines)==len(specs)
for (control,words),line in zip(specs,lines):
 a=words[control&1];b=words[2+((control>>4)&1)];expected=0
 while a:
  bit=a&-a;expected^=b<<(bit.bit_length()-1);a^=bit
 lo,hi=[int(x,16) for x in line.split()];assert expected==lo+(hi<<64)
(out/'reference.txt').write_text(run.stdout)
report={'pass':True,'cases':len(specs),'modes':4,'source_sha256':sha(source),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'compile_command':cmd,'scope':'Canonical x86 128-bit VEX carryless multiply under Rosetta versus Python GF(2), four selectors,64 edge pairs/4096 one-hot basis pairs/4096 random vectors per selector. Independent arithmetic evidence for a128-bit lane; not direct VEX256/hardwareIntel/fullDLL/gameplay proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(specs),'canonical128 carryless multiply cases')
