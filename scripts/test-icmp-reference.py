#!/usr/bin/env python3
"""Canonical packed compares against Python signed/equality masks."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/icmp-reference';out.mkdir(parents=True,exist_ok=True);rng=random.Random(25034);requests=[];expected=[]
for mode in range(12):
 op=mode//4;wide=mode%4>=2
 for i in range(65536 if op<2 else 69632):
  data=[bytes(rng.getrandbits(8) for _ in range(32)) for _ in range(3)]
  if op<2:data[1]=bytes([i//256])*32;data[2]=bytes([i%256])*32
  else:
   x=i&65535;data[1]=b''.join(((x+j)&65535).to_bytes(2,'little') for j in range(16));data[2]=data[1] if i<65536 else b''.join((((x+j)&65535)^(1<<((i+j)%16))).to_bytes(2,'little') for j in range(16))
  vectors=[[int.from_bytes(row[j:j+8],'little') for j in range(0,32,8)] for row in data];setting=i%16;sticky=[0,1,31,63][(i//16)%4];control=0x1f80|((setting&3)<<13)|(0x40 if setting&4 else 0)|(0x8000 if setting&8 else 0)|sticky;flags=[0,0x8d5,0x41,0x884][i%4];word=rng.getrandbits(32);requests.append(f'{mode} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for row in vectors for v in row)+' 0\n');n=32 if wide else 16;result=bytearray(32)
  for j in range(0,n,2 if op==2 else 1):
   if op==0:yes=int.from_bytes(data[1][j:j+1],'little',signed=True)>int.from_bytes(data[2][j:j+1],'little',signed=True)
   elif op==1:yes=data[1][j]==data[2][j]
   else:yes=data[1][j:j+2]==data[2][j:j+2]
   result[j:j+(2 if op==2 else 1)]=bytes([255 if yes else 0])*(2 if op==2 else 1)
  expected.append([int.from_bytes(result[j:j+8],'little') for j in range(0,32,8)]+[v for row in vectors[1:] for v in row]+[word,control,flags]+vectors[2])
request=''.join(requests);(out/'requests.txt').write_text(request);source=R/'tests/icmp_oracle.c';exe=out/'oracle-x86';subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True);run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,check=True,timeout=120);(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(reply,want) in enumerate(zip(replies,expected)):assert [int(x,16) for x in reply.split()]==want,(i,reply,want)
r={'pass':True,'state':'finished','cases':len(expected),'modes':12,'source_sha256':sha(source),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'scope':'Rosetta packedGTB/EQB/EQW bothwidths/REG/MEM versus Python signed/equality masks; all65536bytepairs perapplicablemode, allwordvalues equal and4096singlebitdifference groups perwordmode, full3YMM/flags/maskedMXCSR/memory. Separate native corpus, not physicalIntel/fullDLLinit/gameplay proof.'};(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: %d canonical packed comparison reference cases'%len(expected))
