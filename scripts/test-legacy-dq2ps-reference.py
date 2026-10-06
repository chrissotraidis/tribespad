#!/usr/bin/env python3
"""Canonical legacy packed multiplication and independent exact per-lane arithmetic."""
import hashlib,json,pathlib,random,subprocess
from i32_float_reference import convert
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/legacy-dq2ps-reference';out.mkdir(exist_ok=True);rng=random.Random(0x25034)
vectors=[]
def add(raw,bits,sticky):
 control=0x1f80|((bits&3)<<13)|(0x40 if bits&4 else 0)|(0x8000 if bits&8 else 0)|sticky
 vectors.append((control,[rng.getrandbits(32) for _ in range(4)],raw))
for power in range(32):
 for delta in [-257,-129,-128,-127,-65,-64,-63,-3,-2,-1,0,1,2,3,63,64,65,127,128,129,255,256,257]:
  for sign in [1,-1]:
   raw=((1<<power)+delta)*sign&0xffffffff
   for bits in range(16):
    for sticky in [0,63]:add([raw,(-raw)&0xffffffff,(raw+1)&0xffffffff,(raw-1)&0xffffffff],bits,sticky)
for power in range(24,31):
 shift=power-23
 for parity in range(2):
  for rem in range(1<<shift):
   for sign in [1,-1]:
    raw=sign*((1<<power)+(parity<<shift)+rem)&0xffffffff
    for mode in range(4):
     for sticky in [0,63]:add([raw]*4,mode,sticky)
for base in range(-32768,32768,4):add([(base+j)&0xffffffff for j in range(4)],base&15,0)
for i in range(8192):add([rng.getrandbits(32) for _ in range(4)],i&15,i%64)
requests=[];specs=[]
for mode in range(2):
 for control,a,b in vectors:
  flags=rng.getrandbits(32)&0x8d5;dest=a+[rng.getrandbits(32) for _ in range(4)];source=b+[rng.getrandbits(32) for _ in range(4)];requests.append(f'{mode} {control:x} {flags:x} '+' '.join(f'{x:x}' for x in dest+source)+'\n');specs.append((control,a,b,flags,dest))
text=''.join(requests);(out/'requests.txt').write_text(text);c=R/'tests/legacy_dq2ps_oracle.c';exe=out/'reference';sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();subprocess.run(['clang','-target','x86_64-apple-macos12','-isysroot',sdk,'-std=c11','-O2','-mavx','-mno-red-zone','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True);run=subprocess.run([str(exe)],input=text,capture_output=True,text=True,timeout=60);assert not run.returncode and not run.stderr,(run.returncode,run.stderr);lines=run.stdout.splitlines();assert len(lines)==len(specs);rows=[]
for spec,line in zip(specs,lines):
 control,a,b,flags,dest=spec;vals=[int(x,16) for x in line.split()];results=[];status=0
 for raw in b:
  value,raised=convert(raw,control);results.append(value);status|=raised
 assert vals==[control|status,flags]+results+dest[4:],(spec,vals,[control|status,flags]+results+dest[4:]);rows.append((control,b,results,control|status))
(out/'reference.txt').write_text(run.stdout);(out/'vectors.json').write_text(json.dumps(rows[:len(vectors)])+'\n');assembly=subprocess.check_output(['otool','-tvV',str(exe)],text=True);(out/'disassembly.txt').write_text(assembly);assert assembly.count('\tcvtdq2ps\t')==2 and '\tvcvtdq2ps\t' not in assembly;report={'pass':True,'cases':len(specs),'numeric_vectors':len(vectors),'source_sha256':{str(p.relative_to(R)):sha(p) for p in [c,pathlib.Path(__file__).resolve(),R/'scripts/i32_float_reference.py',R/'scripts/f32_packed_reference.py']},'requests_sha256':sha(out/'requests.txt'),'results_sha256':sha(out/'reference.txt'),'vectors_sha256':sha(out/'vectors.json'),'binary_sha256':sha(exe),'scope':'Fresh legacy CVTDQ2PS REG/alignedMEM128 Rosetta reference and independent exact rational signed-dword-to-float rounding. All signed powers-of-two neighboring boundaries, all discarded-bit remainders/coefficient parities/signs/allrounding modes, complete signed16bit range, random mixed-lane vectors, DAZ/FTZ/sticky precision status and source/YMM/flags preservation. Masked reference; not physicalIntel/unmasked Windows delivery/fullDLL/gameplay proof.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(specs),'canonical legacy packed signed-int conversion checks;',len(vectors),'numeric vectors')
