#!/usr/bin/env python3
"""Canonical legacy two-lane widening and independent binary64 value/status oracle."""
import hashlib,json,pathlib,random,struct,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/legacy-widen-reference';out.mkdir(exist_ok=True)
def expected(v,control):
 mag=v&0x7fffffff;exp=mag>>23;fraction=mag&0x7fffff;sign=(v>>31)<<63
 if exp==255:return sign|0x7ff0000000000000|(fraction<<29)|(0x8000000000000 if fraction else 0),int(bool(fraction) and not fraction&0x400000)
 if control&0x40 and 0<mag<0x800000:return sign,0
 value=struct.unpack('<f',struct.pack('<I',v))[0];return struct.unpack('<Q',struct.pack('<d',value))[0],2 if 0<mag<0x800000 else 0
rng=random.Random(0x25034);edge=[0,0x80000000,1,0x80000001,0x7fffff,0x807fffff,0x800000,0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0xffc12345,0x7fa12345,0xffa12345];vectors=[]
for a in edge:
 for b in edge:
  for bits in range(16):
   for sticky in [0,63]:vectors.append((0x1f80|((bits&3)<<13)|(0x40 if bits&4 else 0)|(0x8000 if bits&8 else 0)|sticky,a,b))
for exponent in range(256):
 for sign in range(2):
  for fraction in [0,1,0x3fffff,0x400000,0x7fffff]:
   for daz in [0,64]:vectors.append((0x1f80|daz,(sign<<31)|(exponent<<23)|fraction,edge[(exponent+fraction)%len(edge)]))
for i in range(4096):vectors.append((0x1f80|((i&3)<<13)|(0x40 if i&4 else 0)|(0x8000 if i&8 else 0)|(i%64),rng.getrandbits(32),rng.getrandbits(32)))
requests=[];specs=[]
for mode in range(2):
 for control,a,b in vectors:
  flags=rng.getrandbits(32)&0x8d5;dest=[rng.getrandbits(32) for _ in range(8)];source=[a,b]+[rng.getrandbits(32) for _ in range(6)];requests.append(f'{mode} {control:x} {flags:x} '+' '.join(f'{x:x}' for x in dest+source)+'\n');specs.append((control,a,b,flags,dest))
text=''.join(requests);(out/'requests.txt').write_text(text);c=R/'tests/legacy_widen_oracle.c';exe=out/'reference';sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();cmd=['clang','-target','x86_64-apple-macos12','-isysroot',sdk,'-std=c11','-O2','-mavx','-mno-red-zone','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)];subprocess.run(cmd,check=True);run=subprocess.run([str(exe)],input=text,capture_output=True,text=True,timeout=60);assert not run.returncode and not run.stderr,(run.returncode,run.stderr);lines=run.stdout.splitlines();assert len(lines)==len(specs);rows=[]
for spec,line in zip(specs,lines):
 control,a,b,flags,dest=spec;vals=[int(x,16) for x in line.split()];v0,s0=expected(a,control);v1,s1=expected(b,control);want=list(struct.unpack('<4I',struct.pack('<2Q',v0,v1)))+dest[4:];assert vals==[control|s0|s1,flags]+want,(spec,vals,[control|s0|s1,flags]+want);rows.append((control,a,b,v0,v1,control|s0|s1))
(out/'reference.txt').write_text(run.stdout);(out/'vectors.json').write_text(json.dumps(rows[:len(vectors)])+'\n');assembly=subprocess.check_output(['otool','-tvV',str(exe)],text=True);(out/'disassembly.txt').write_text(assembly);assert assembly.count('cvtps2pd')==2 and 'vcvtps2pd' not in assembly;report={'pass':True,'cases':len(specs),'numeric_vectors':len(vectors),'source_sha256':{str(p.relative_to(R)):sha(p) for p in [c,pathlib.Path(__file__).resolve()]},'requests_sha256':sha(out/'requests.txt'),'results_sha256':sha(out/'reference.txt'),'vectors_sha256':sha(out/'vectors.json'),'binary_sha256':sha(exe),'scope':'Legacy canonical REG and unalignedMEM64 CVTPS2PD under Rosetta; exact Python binary32-to-binary64 widening plus explicit NaN/status oracle, masked mixed lane IE/DE, rounding/DAZ/FTZ/sticky flags and YMM/source preservation. Not unmasked packed priority/physical Intel/Windows delivery/gameplay proof.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(specs),'canonical legacy widening checks;',len(vectors),'numeric vectors')
