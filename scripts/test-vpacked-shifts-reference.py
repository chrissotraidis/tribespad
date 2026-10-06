#!/usr/bin/env python3
"""Canonical packed shifts versus Python element/128-bit lane integers."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/vpacked-shifts-reference';out.mkdir(parents=True,exist_ok=True);rng=random.Random(25034);requests=[];expected=[];counts=[0,1,15,16,30,31,32,33,63,64,65,255,256,0xffffffff,0x100000000,0xffffffffffffffff]
for mode in range(20):
 immediate_mode=mode<8;op=mode//2 if immediate_mode else 0 if mode<12 else 1 if mode<16 else 4;wide=mode%2 if immediate_mode else mode%4>=2
 for i in range(8192 if immediate_mode else 2048):
  immediate=i//32 if immediate_mode else 0;data=[bytes(rng.getrandbits(8) for _ in range(32)) for _ in range(3)];sample=i%32
  if sample<16:data[1]=bytes([(j+sample*17)&255 for j in range(32)])
  if not immediate_mode:
   if op==4:data[2]=b''.join((counts[(i+j)%len(counts)]&0xffffffff).to_bytes(4,'little') for j in range(8))
   else:data[2]=counts[i%len(counts)].to_bytes(8,'little')+data[2][8:]
  vectors=[[int.from_bytes(row[j:j+8],'little') for j in range(0,32,8)] for row in data];setting=i%16;sticky=[0,1,31,63][(i//16)%4];control=0x1f80|((setting&3)<<13)|(0x40 if setting&4 else 0)|(0x8000 if setting&8 else 0)|sticky;flags=[0,0x8d5,0x41,0x884][i%4];word=rng.getrandbits(32);requests.append(f'{mode} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for row in vectors for v in row)+f' {immediate:x}\n');n=32 if wide else 16;result=bytearray(32);count=immediate if immediate_mode else int.from_bytes(data[2][:8],'little')
  if op in [0,4]:
   for j in range(0,n,4):
    k=int.from_bytes(data[2][j:j+4],'little') if op==4 else count;value=(int.from_bytes(data[1][j:j+4],'little')<<k)&0xffffffff if k<32 else 0;result[j:j+4]=value.to_bytes(4,'little')
  elif op==1:
   for j in range(0,n,8):result[j:j+8]=(int.from_bytes(data[1][j:j+8],'little')>>count if count<64 else 0).to_bytes(8,'little')
  else:
   for j in range(0,n,16):
    value=int.from_bytes(data[1][j:j+16],'little');value=((value<<(count*8))&((1<<128)-1)) if op==2 and count<16 else value>>(count*8) if op==3 and count<16 else 0;result[j:j+16]=value.to_bytes(16,'little')
  expected.append([int.from_bytes(result[j:j+8],'little') for j in range(0,32,8)]+[v for row in vectors[1:] for v in row]+[word,control,flags]+vectors[2])
request=''.join(requests);(out/'requests.txt').write_text(request);source=R/'tests/vpacked_shifts_oracle.c';table=R/'tests/vpacked_shifts_oracle.inc';exe=out/'oracle-x86';subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True);run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,check=True,timeout=120);(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(reply,want) in enumerate(zip(replies,expected)):assert [int(x,16) for x in reply.split()]==want,(i,reply,want)
r={'pass':True,'state':'finished','cases':len(expected),'modes':20,'source_sha256':sha(source),'table_sha256':sha(table),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'scope':'Rosetta canonical all256 immediate values for four shifts/bothwidths plus scalar64 REG/MEM counts and AVX2 per-dword REG/MEM counts; Python element/whole128-bit lane integer model. Includes counts beyondwidth, low64 significance/ignored high64, unsigned lane counts, all3YMM/MXCSR/flags/memory. Separate native corpus, not physical Intel/full initialization/gameplay proof.'};(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: %d canonical packed shift reference cases'%len(expected))
