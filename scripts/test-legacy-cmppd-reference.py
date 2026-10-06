#!/usr/bin/env python3
"""Fresh legacy CMPPD, all eight predicates and all256 raw immediates."""
import hashlib,json,pathlib,random,subprocess,re
from f64_predicate_reference import compare
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/legacy-cmppd-reference';out.mkdir(exist_ok=True);rng=random.Random(0x25034)
edge=[0x3fe0000000000000,0xbfe0000000000000,0x3ff8000000000000,0xbff8000000000000,0xbfefffffffffffff,0xbff0000000000001,0,0x8000000000000000,1,0x8000000000000001,0xfffffffffffff,0x800fffffffffffff,0x10000000000000,0x8010000000000000,0x3ff0000000000000,0xbff0000000000000,0x4000000000000000,0x4010000000000000,0x3fefffffffffffff,0x3ff0000000000001,0x7fefffffffffffff,0xffefffffffffffff,0x7ff0000000000000,0xfff0000000000000,0x7ff8123456789abc,0xfff8123456789abc,0x7ff0123456789abc,0xfff0123456789abc];vectors=[]
for predicate in range(8):
 for aa in edge:
  for bb in edge:
   for bits in range(16):
    for sticky in [0,63]:vectors.append((predicate,0x1f80|((bits&3)<<13)|(64 if bits&4 else 0)|(0x8000 if bits&8 else 0)|sticky,aa,bb))
for exp in range(2048):
 for sign in range(2):
  for fraction in [0,1,0x7ffffffffffff,0x8000000000000,0xfffffffffffff]:
   for daz in [0,64]:vectors.append((exp%8,0x1f80|daz|(exp%4<<13),(sign<<63)|(exp<<52)|fraction,edge[(exp+fraction)%len(edge)]))
for i in range(16384):vectors.append((i%8,0x1f80|((i&3)<<13)|(64 if i&4 else 0)|(0x8000 if i&8 else 0)|(i%64),rng.getrandbits(64),rng.getrandbits(64)))
for predicate in range(256):
 for i in range(16):vectors.append((predicate,0x1f80|(64 if i&1 else 0),edge[(i*3)%len(edge)],edge[(i*5+1)%len(edge)]))
packed=[(predicate,control,aa,bb,edge[(i*7)%len(edge)],edge[(i*13+3)%len(edge)]) for i,(predicate,control,aa,bb) in enumerate(vectors)]
requests=[];specs=[]
for mode in range(2):
 for predicate,control,aa,bb,cc,dd in packed:
  flags=rng.getrandbits(32)&0x8d5;dest=[aa&0xffffffff,aa>>32,cc&0xffffffff,cc>>32]+[rng.getrandbits(32) for _ in range(4)];source=[bb&0xffffffff,bb>>32,dd&0xffffffff,dd>>32]+[rng.getrandbits(32) for _ in range(4)];requests.append(f'{mode} {predicate} {control:x} {flags:x} '+' '.join(f'{x:x}' for x in dest+source)+'\n');specs.append((predicate,control,aa,bb,cc,dd,flags,dest))
text=''.join(requests);(out/'requests.txt').write_text(text);c=R/'tests/legacy_cmppd_oracle.c';exe=out/'reference';sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();subprocess.run(['clang','-target','x86_64-apple-macos12','-isysroot',sdk,'-std=c11','-O2','-mavx','-mno-red-zone','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True);run=subprocess.run([str(exe)],input=text,capture_output=True,text=True,timeout=120);assert not run.returncode and not run.stderr,(run.returncode,run.stderr);lines=run.stdout.splitlines();assert len(lines)==len(specs);rows=[];deviations=[]
for spec,line in zip(specs,lines):
 predicate,control,aa,bb,cc,dd,flags,dest=spec;vals=[int(x,16) for x in line.split()];v0,s0=compare(aa,bb,predicate,control);v1,s1=compare(cc,dd,predicate,control);expected=[control|s0|s1,flags,v0&0xffffffff,v0>>32,v1&0xffffffff,v1>>32]+dest[4:]
 if vals!=expected:
  def mixed_nan_denormal(x,y):
   a=x&0x7fffffffffffffff;b=y&0x7fffffffffffffff
   return a>0x7ff0000000000000 and 0<b<0x10000000000000 or b>0x7ff0000000000000 and 0<a<0x10000000000000
  assert predicate&7 in [3,7] and not control&64 and (mixed_nan_denormal(aa,bb) or mixed_nan_denormal(cc,dd)) and vals==[expected[0]|2]+expected[1:],(spec,vals,expected)
  deviations.append({'index':len(rows),'predicate':predicate,'inputs':[control,aa,bb,cc,dd],'expected_mxcsr':expected[0],'rosetta_mxcsr':vals[0]})
 rows.append((predicate,control,aa,bb,cc,dd,v0,v1,control|s0|s1))
(out/'oracle-deviations.json').write_text(json.dumps(deviations,indent=2)+'\n');(out/'reference.txt').write_text(run.stdout);(out/'vectors.json').write_text(json.dumps(rows[:len(vectors)])+'\n');assembly=subprocess.check_output(['otool','-tvV',str(exe)],text=True);(out/'disassembly.txt').write_text(assembly);matches=re.findall(r'\t(cmp[a-z]*pd)\t',assembly);assert len(matches)==512 and '\tvcmppd\t' not in assembly,len(matches);report={'pass':True,'cases':len(specs),'numeric_vectors':len(vectors),'rosetta_status_deviations':len(deviations),'oracle_deviations_sha256':sha(out/'oracle-deviations.json'),'source_sha256':{str(p.relative_to(R)):sha(p) for p in [c,pathlib.Path(__file__).resolve(),R/'scripts/f64_predicate_reference.py',R/'ref/intel/floating-reference-2.13.pdf',R/'ref/bochs/bochs/cpu/softfloat3e/f64_compare.cc',R/'ref/bochs/bochs/cpu/sse_pfp.cc']},'requests_sha256':sha(out/'requests.txt'),'results_sha256':sha(out/'reference.txt'),'vectors_sha256':sha(out/'vectors.json'),'binary_sha256':sha(exe),'scope':'Fresh masked legacy CMPPD REG/alignedMEM128, all8 predicates over full edge/control pairs; all2048 exponent/sign/fraction boundaries and16384 randoms with distributed predicates; all256 raw immediate values. NaN/denormal status differences for Rosetta predicates3/7 explicitly excluded and recorded; Intel NaN-first flow and pinned Bochs comparison support the candidate. Independent finite Python numeric comparisons and explicit NaN signaling/DAZ/DE masks, both packed lanes and sticky/source/upperYMM/flags/memory preservation. Not physicalIntel/unmasked Windows delivery/emitted platform/fullDLL/gameplay proof.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(specs),'canonical legacy CMPPD checks;',len(vectors),'paired vectors')
