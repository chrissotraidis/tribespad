#!/usr/bin/env python3
"""Compare canonical VPSUBUSW with Python unsigned saturation."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/subusw-reference';out.mkdir(parents=True,exist_ok=True);rng=random.Random(25034);requests=[];expected=[];edge=[0,1,2,0x7fff,0x8000,0xfffd,0xfffe,0xffff]
for mode in range(4):
 for i in range(69696):
  data=[bytes(rng.getrandbits(8) for _ in range(32)) for _ in range(3)];left=[];right=[]
  for j in range(16):
   if i<64:x=edge[(i//8+j)%8];y=edge[(i%8+j*3)%8]
   elif i<65600:x=(i-64+j)&65535;y=edge[(i+j)%8]
   else:x=rng.getrandbits(16);y=rng.getrandbits(16)
   left.append(x);right.append(y)
  data[1]=b''.join(x.to_bytes(2,'little') for x in left);data[2]=b''.join(y.to_bytes(2,'little') for y in right);vectors=[[int.from_bytes(row[j:j+8],'little') for j in range(0,32,8)] for row in data];setting=i%16;sticky=[0,1,31,63][(i//16)%4];control=0x1f80|((setting&3)<<13)|(0x40 if setting&4 else 0)|(0x8000 if setting&8 else 0)|sticky;flags=[0,0x8d5,0x41,0x884][i%4];word=rng.getrandbits(32);requests.append(f'{mode} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for row in vectors for v in row)+' 0\n');result=b''.join(max(0,x-y).to_bytes(2,'little') for x,y in zip(left,right));result=result if mode>=2 else result[:16]+bytes(16)
  expected.append([int.from_bytes(result[j:j+8],'little') for j in range(0,32,8)]+[v for row in vectors[1:] for v in row]+[word,control,flags]+vectors[2])
request=''.join(requests);(out/'requests.txt').write_text(request);source=R/'tests/subusw_oracle.c';exe=out/'oracle-x86';subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True);run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,check=True,timeout=120);(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(reply,want) in enumerate(zip(replies,expected)):assert [int(x,16) for x in reply.split()]==want,(i,reply,want)
r={'pass':True,'state':'finished','cases':len(expected),'modes':4,'source_sha256':sha(source),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'scope':'Canonical Rosetta VPSUBUSW bothwidths/REG/MEM versus Python unsigned saturation; all64cornerpairs/all65536leftwordvalues with boundaryrightwords/4096randomvectorpairs per mode, full3YMM/upperzero128/flags/maskedMXCSR/source memory. Separate native corpus; not all2^32wordpairs/physicalIntel/fullDLLinit/gameplay proof.'};(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: %d canonical VPSUBUSW cases'%len(expected))
