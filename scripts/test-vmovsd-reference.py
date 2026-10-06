#!/usr/bin/env python3
"""Verify canonical actual x86 scalar double moves with independent integer bit selection."""
import hashlib,json,pathlib,random,struct,subprocess
R=pathlib.Path(__file__).resolve().parents[1];D=R/'generated/sse/vmovsd-reference';D.mkdir(exist_ok=True);source=R/'tests/vmovsd_oracle.c';exe=D/'oracle-x86';subprocess.run(['clang','-arch','x86_64','-mavx','-mno-red-zone','-O2','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True)
rng=random.Random(0x42b);edges=[0,0x8000000000000000,1,0x8000000000000001,0x7ff0000000000000,0xfff0000000000000,0x7ff0123456789abc,0xfff0123456789abc,0x7ff8123456789abc,0xfff8123456789abc,0x7fefffffffffffff,0x000fffffffffffff,0x0010000000000000,0xffffffffffffffff,0x0123456789abcdef,0x89abcdef01234567];requests=[];expected=[]
for mode in range(4):
 n=8
 for edge in edges:
  for control in range(16):
   for sticky in [0,1,31,63]:
    flags=rng.getrandbits(32);mxcsr=0x1f80|(control%4<<13)|(0x40 if control&4 else 0)|(0x8000 if control&8 else 0)|sticky
    vectors=bytearray(rng.getrandbits(8) for _ in range(96));vectors[64:72]=struct.pack('<Q',edge);before=bytes(vectors);out=bytearray(before);memory=before[64:96]
    if mode in (0,1):out[:32]=before[64:72]+before[40:48]+bytes(16)
    elif mode==2:out[:32]=before[64:72]+bytes(24)
    word=rng.getrandbits(32)
    requests.append(f'{mode} {mxcsr:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for v in struct.unpack('<12Q',before))+' 0\n');expected.append((bytes(out),mxcsr,flags&0x8d5,word,memory))
request=''.join(requests);(D/'requests.txt').write_text(request);run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,timeout=60);assert run.returncode==0 and not run.stderr,(run.returncode,run.stderr);(D/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(line,(wanted,control,flags,word,memory)) in enumerate(zip(replies,expected)):
 values=[int(v,16) for v in line.split()];assert len(values)==19;assert struct.pack('<12Q',*values[:12])==wanted,(i,line);assert values[12:15]==[word,control,flags],(i,line);assert struct.pack('<4Q',*values[15:])==memory,(i,line)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();report={'pass':True,'cases':len(expected),'cases_per_mode':len(expected)//4,'source_sha256':sha(source),'verifier_sha256':sha(pathlib.Path(__file__)),'binary_sha256':sha(exe),'requests_sha256':sha(D/'requests.txt'),'reference_sha256':sha(D/'reference.txt'),'scope':'Direct canonical x86 scalar double moves through Rosetta vs independent integer bit-copy expectations; vectors, upper halves, all-masked MXCSR/sticky flags and guarded source-memory capture. Native alias/state/memory checks are separate. Not physical Intel or full DLL/gameplay proof.'};(D/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
