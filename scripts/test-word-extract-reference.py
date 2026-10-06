#!/usr/bin/env python3
"""Canonical legacy word extraction under Rosetta, both REG encodings/MEM16."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/word-extract-reference';out.mkdir(exist_ok=True)
(out/'word_oracle_cases.inc').write_text(''.join(f'case {i}: if(mode==0){{RUN("pextrw ${i}, %%xmm1, %%eax");}}else if(mode==1){{RUN(".byte 0x66,0x0f,0x3a,0x15,0xc8,{i}");}}else{{RUN("pextrw ${i}, %%xmm1, %[memory]");}}break;\n' for i in range(256)))
rng=random.Random(0xd064);specs=[]
for mode in range(3):
 for imm in range(256):
  for sample in range(32):
   inputs=[0]*8 if sample==0 else [0xffffffff]*8 if sample==1 else [rng.getrandbits(32) for _ in range(8)]
   merges=[rng.getrandbits(32) for _ in range(8)];scalar=rng.getrandbits(32);control=0x1f80|((sample%4)<<13)|(sample%64)|((sample%2)<<6)|((sample//2%2)<<15);flags=rng.getrandbits(32)&0x8d5
   specs.append((mode,imm,control,flags,scalar,inputs,merges))
requests=''.join('%d %d %x %x %x '%tuple(v[:5])+' '.join('%08x'%x for x in v[5]+v[6])+'\n' for v in specs);(out/'requests.txt').write_text(requests)
source=R/'tests/word_extract_oracle.c';exe=out/'reference';sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();cmd=['clang','-target','x86_64-apple-macos12','-isysroot',sdk,'-O2','-mavx','-msse4.1','-Wall','-Wextra','-Werror','-I',str(out),str(source),'-o',str(exe)];subprocess.run(cmd,check=True)
run=subprocess.run([str(exe)],input=requests,capture_output=True,text=True,timeout=60);assert run.returncode==0 and not run.stderr,(run.returncode,run.stderr);lines=run.stdout.splitlines();assert len(lines)==len(specs)
for spec,line in zip(specs,lines):
 mode,imm,control,flags,scalar,inputs,merges=spec;values=[int(x,16) for x in line.split()];output=inputs[:];result=0xdeadbeef;memory=scalar&65535;word=(inputs[(imm%8)//2]>>(16*(imm%2)))&65535
 if mode<2:result=word
 else:memory=word
 assert values==[result,control,flags,memory]+output,(spec,values)
(out/'reference.txt').write_text(run.stdout);assembly=subprocess.check_output(['otool','-tvV',str(exe)],text=True);(out/'disassembly.txt').write_text(assembly);assert assembly.count('pextrw')==768
report={'pass':True,'cases':len(specs),'source_sha256':{str(p.relative_to(R)):sha(p) for p in [source,pathlib.Path(__file__).resolve()]},'fixture_sha256':sha(out/'word_oracle_cases.inc'),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'compile_command':cmd,'scope':'Canonical legacy XMM REG extraction in both encodings and MEM16 stores under Rosetta, all256imm8 and32 edge/random vectors per mode, YMM preservation, flags/MXCSR and2byte guards. Disassembly confirms768 PEXTRW. Not physical Intel/fullDLL/gameplay proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(specs),'canonical legacy word extraction cases')
