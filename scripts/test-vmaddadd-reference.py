#!/usr/bin/env python3
"""Canonical packed multiply/add instructions versus Python widened integers."""
import hashlib,itertools,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/vmaddadd-reference';out.mkdir(parents=True,exist_ok=True);rng=random.Random(25034);requests=[];expected=[]
products={a*b:(a,b) for a in range(256) for b in range(-128,128)};corners=[(255,127,255,127),(255,-128,255,-128)]
for target in [32765,32766,32767,32768,32769,32770,-32765,-32766,-32767,-32768,-32769,-32770]:
 for product,(a,b) in products.items():
  if target-product in products:c,d=products[target-product];corners.append((a,b,c,d));break
 else:raise AssertionError(target)
words=list(itertools.product([-32768,-32767,-1,0,1,32767],repeat=4));adds=list(itertools.product([0,1,0xffffffff,0x80000000,0x7fffffff,0x80008000,0x7fff7fff,0xffff0001,0x00ff00ff,0x7f807f80,0x55555555,0xaaaaaaaa],repeat=2))
for mode in range(16):
 op=mode//4;fixed=65536+len(corners) if op==0 else len(words) if op==1 else len(adds) if op==2 else 65536
 for i in range(fixed+4096):
  data=[bytes(rng.getrandbits(8) for _ in range(32)) for _ in range(3)]
  if i<fixed:
   if op==0:
    a,b,c,d=(i>>8,(i&255)-128,i>>8,(i&255)-128) if i<65536 else corners[i-65536];data[1]=bytes([a,c])*16;data[2]=bytes([b&255,d&255])*16
   elif op==1:
    a,b,c,d=words[i];data[1]=(a.to_bytes(2,'little',signed=True)+c.to_bytes(2,'little',signed=True))*8;data[2]=(b.to_bytes(2,'little',signed=True)+d.to_bytes(2,'little',signed=True))*8
   elif op==2:
    a,b=adds[i];data[1]=a.to_bytes(4,'little')*8;data[2]=b.to_bytes(4,'little')*8
   else:data[1]=bytes([i>>8])*32;data[2]=bytes([i&255])*32
  vectors=[[int.from_bytes(row[j:j+8],'little') for j in range(0,32,8)] for row in data];setting=i%16;sticky=[0,1,31,63][(i//16)%4];control=0x1f80|((setting&3)<<13)|(0x40 if setting&4 else 0)|(0x8000 if setting&8 else 0)|sticky;flags=[0,0x8d5,0x41,0x884][i%4];word=rng.getrandbits(32)
  requests.append(f'{mode} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for row in vectors for v in row)+' 0\n');n=32 if mode%4>=2 else 16;result=bytearray(32)
  if op==0:
   for j in range(0,n,2):
    value=sum(data[1][k]*int.from_bytes(data[2][k:k+1],'little',signed=True) for k in [j,j+1]);value=max(-32768,min(32767,value));result[j:j+2]=value.to_bytes(2,'little',signed=True)
  elif op==1:
   for j in range(0,n,4):
    value=sum(int.from_bytes(data[1][k:k+2],'little',signed=True)*int.from_bytes(data[2][k:k+2],'little',signed=True) for k in [j,j+2]);result[j:j+4]=(value&0xffffffff).to_bytes(4,'little')
  elif op==2:
   for j in range(0,n,4):result[j:j+4]=((int.from_bytes(data[1][j:j+4],'little')+int.from_bytes(data[2][j:j+4],'little'))&0xffffffff).to_bytes(4,'little')
  else:
   for j in range(n):result[j]=(data[1][j]+data[2][j])&255
  expected.append([int.from_bytes(result[j:j+8],'little') for j in range(0,32,8)]+[v for row in vectors[1:] for v in row]+[word,control,flags]+vectors[2])
request=''.join(requests);(out/'requests.txt').write_text(request);source=R/'tests/vmaddadd_oracle.c';exe=out/'oracle-x86';subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True);run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,check=True,timeout=120);(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(reply,want) in enumerate(zip(replies,expected)):assert [int(x,16) for x in reply.split()]==want,(i,reply,want)
r={'pass':True,'state':'finished','cases':len(expected),'modes':16,'source_sha256':sha(source),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'scope':'Rosetta canonical128/256 REG/MEM against Python widened product/addition rules: all65536 unsigned/signed byte products and byte-add pairs, exact saturation neighborhoods/extremes,1296 signed-word corner quadruples including80000000 overflow,144 dword-add boundary pairs and4096 mixed groups per mode. Full3YMM/flags/MXCSR/memory; separate native corpus, not physical Intel/full initialization/gameplay proof.'};(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: %d canonical packed multiply-add/add reference cases'%len(expected))
