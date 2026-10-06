#!/usr/bin/env python3
"""Fresh canonical ordered binary64 comparison and independent status/flags."""
import hashlib,json,pathlib,random,subprocess
from f64_compare_reference import compare
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/legacy-comisd-reference';out.mkdir(exist_ok=True);rng=random.Random(0x25034)
edge=[0,0x8000000000000000,1,0x8000000000000001,0xfffffffffffff,0x800fffffffffffff,0x10000000000000,0x8010000000000000,0x3ff0000000000000,0xbff0000000000000,0x4000000000000000,0x4010000000000000,0x3fefffffffffffff,0x3ff0000000000001,0x7fefffffffffffff,0xffefffffffffffff,0x7ff0000000000000,0xfff0000000000000,0x7ff8123456789abc,0xfff8123456789abc,0x7ff0123456789abc,0xfff0123456789abc];vectors=[]
for a in edge:
 for b in edge:
  for bits in range(16):
   for sticky in [0,63]:vectors.append((0x1f80|((bits&3)<<13)|(0x40 if bits&4 else 0)|(0x8000 if bits&8 else 0)|sticky,a,b))
for exp in range(2048):
 for sign in range(2):
  for fraction in [0,1,0x7ffffffffffff,0x8000000000000,0xfffffffffffff]:
   for daz in [0,64]:vectors.append((0x1f80|daz|(exp%4<<13),(sign<<63)|(exp<<52)|fraction,edge[(exp+fraction)%len(edge)]))
for i in range(16384):vectors.append((0x1f80|((i&3)<<13)|(0x40 if i&4 else 0)|(0x8000 if i&8 else 0)|(i%64),rng.getrandbits(64),rng.getrandbits(64)))
requests=[];specs=[]
for mode in range(2):
 for control,a,b in vectors:
  flags=rng.getrandbits(32)&0x8d5;dest=[a&0xffffffff,a>>32]+[rng.getrandbits(32) for _ in range(6)];source=[b&0xffffffff,b>>32]+[rng.getrandbits(32) for _ in range(6)];requests.append(f'{mode} {control:x} {flags:x} '+' '.join(f'{x:x}' for x in dest+source)+'\n');specs.append((control,a,b,flags,dest))
text=''.join(requests);(out/'requests.txt').write_text(text);c=R/'tests/legacy_comisd_oracle.c';exe=out/'reference';sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();subprocess.run(['clang','-target','x86_64-apple-macos12','-isysroot',sdk,'-std=c11','-O2','-mavx','-mno-red-zone','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True);run=subprocess.run([str(exe)],input=text,capture_output=True,text=True,timeout=60);assert not run.returncode and not run.stderr,(run.returncode,run.stderr);lines=run.stdout.splitlines();assert len(lines)==len(specs);rows=[]
for spec,line in zip(specs,lines):
 control,a,b,flags,dest=spec;af,status=compare(a,b,control);want=[control|status,af]+dest;assert [int(x,16) for x in line.split()]==want,(spec,line,want);rows.append((control,a,b,flags,control|status,af))
(out/'reference.txt').write_text(run.stdout);(out/'vectors.json').write_text(json.dumps(rows[:len(vectors)])+'\n');assembly=subprocess.check_output(['otool','-tvV',str(exe)],text=True);(out/'disassembly.txt').write_text(assembly);assert assembly.count('\tcomisd\t')==2 and '\tvcomisd\t' not in assembly;report={'pass':True,'cases':len(specs),'numeric_vectors':len(vectors),'source_sha256':{str(p.relative_to(R)):sha(p) for p in [c,pathlib.Path(__file__).resolve(),R/'scripts/f64_compare_reference.py']},'requests_sha256':sha(out/'requests.txt'),'results_sha256':sha(out/'reference.txt'),'vectors_sha256':sha(out/'vectors.json'),'binary_sha256':sha(exe),'scope':'Fresh legacy COMISD REG/unalignedMEM64 Rosetta reference; independent finite Python comparison and explicit NaN/DAZ/IE/DE/flags model. All six arithmetic flag outputs, all2048 exponent/sign/fraction boundaries, NaN/zero/subnormal/infinity pairs, RC/DAZ/FTZ/sticky and16384 random pairs; full source/destination XMM/YMM and memory preservation. Not physical Intel/Windows delivery/fullDLL/gameplay proof.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(specs),'canonical legacy ordered-double comparison checks;',len(vectors),'numeric vectors')
