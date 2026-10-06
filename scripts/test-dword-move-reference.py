#!/usr/bin/env python3
"""Canonical W0 VEX dword bit movements under Rosetta; not IA32 W1 proof."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/dword-move-reference';out.mkdir(exist_ok=True)
(out/'dword_oracle_cases.inc').write_text(''.join(f'case {i}: if(mode==0){{RUN("vpextrd ${i}, %%xmm1, %%eax");}}else if(mode==1){{RUN("vpextrd ${i}, %%xmm1, %[memory]");}}else if(mode==2){{RUN("vpinsrd ${i}, %%ecx, %%xmm2, %%xmm1");}}else{{RUN("vpinsrd ${i}, %[memory], %%xmm2, %%xmm1");}}break;\n' for i in range(256)))
rng=random.Random(0xd064);specs=[]
for mode in range(4):
 for imm in range(256):
  for sample in range(32):
   inputs=[0]*8 if sample==0 else [0xffffffff]*8 if sample==1 else [rng.getrandbits(32) for _ in range(8)]
   merges=[rng.getrandbits(32) for _ in range(8)];scalar=rng.getrandbits(32);control=0x1f80|((sample%4)<<13)|(sample%64)|((sample%2)<<6)|((sample//2%2)<<15);flags=rng.getrandbits(32)&0x8d5
   specs.append((mode,imm,control,flags,scalar,inputs,merges))
requests=''.join('%d %d %x %x %x '%tuple(v[:5])+' '.join('%08x'%x for x in v[5]+v[6])+'\n' for v in specs);(out/'requests.txt').write_text(requests)
source=R/'tests/dword_move_oracle.c';exe=out/'reference';sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();cmd=['clang','-target','x86_64-apple-macos12','-isysroot',sdk,'-O2','-mavx','-msse4.1','-Wall','-Wextra','-Werror','-I',str(out),str(source),'-o',str(exe)];subprocess.run(cmd,check=True)
run=subprocess.run([str(exe)],input=requests,capture_output=True,text=True,timeout=60);assert run.returncode==0 and not run.stderr,(run.returncode,run.stderr);lines=run.stdout.splitlines();assert len(lines)==len(specs)
for spec,line in zip(specs,lines):
 mode,imm,control,flags,scalar,inputs,merges=spec;values=[int(x,16) for x in line.split()];output=inputs[:];result=0xdeadbeef;memory=scalar
 if mode==0:result=inputs[imm%4]
 elif mode==1:memory=inputs[imm%4]
 else:output=merges[:4]+[0]*4;output[imm%4]=scalar
 assert values==[result,control,flags,memory]+output,(spec,values)
(out/'reference.txt').write_text(run.stdout);assembly=subprocess.check_output(['otool','-tvV',str(exe)],text=True);(out/'disassembly.txt').write_text(assembly);assert assembly.count('vpextrd')==512 and assembly.count('vpinsrd')==512
report={'pass':True,'cases':len(specs),'source_sha256':{str(p.relative_to(R)):sha(p) for p in [source,pathlib.Path(__file__).resolve()]},'fixture_sha256':sha(out/'dword_oracle_cases.inc'),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'compile_command':cmd,'scope':'Canonical W0 VEX REG/MEM extraction/insertion under Rosetta, all256imm8,32 edge/random vectors permode; full YMM result/merge preservation, flags/MXCSR,4byte memory guards. Disassembly confirms512 VPEXTRD and512 VPINSRD instructions. Not direct IA32W1/physicalIntel/fullDLL/gameplay proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(specs),'canonical W0 VEX dword bit-movement cases')
