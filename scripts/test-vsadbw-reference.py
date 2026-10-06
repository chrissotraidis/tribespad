#!/usr/bin/env python3
"""Canonical VPSADBW x86 instructions versus Python unsigned byte group sums."""
import hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/vsadbw-reference';out.mkdir(parents=True,exist_ok=True);rng=random.Random(25034);requests=[];expected=[]
for mode in range(4):
 for i in range(65536+1024):
  data=[bytes(rng.getrandbits(8) for _ in range(32)) for _ in range(3)]
  if i<65536:data[1]=bytes([i>>8])*32;data[2]=bytes([i&255])*32
  vectors=[[int.from_bytes(row[j:j+8],'little') for j in range(0,32,8)] for row in data];setting=i%16;sticky=[0,1,31,63][(i//16)%4];control=0x1f80|((setting&3)<<13)|(0x40 if setting&4 else 0)|(0x8000 if setting&8 else 0)|sticky;flags=[0,0x8d5,0x41,0x884][i%4];word=rng.getrandbits(32)
  requests.append(f'{mode} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for row in vectors for v in row)+' 0\n');n=32 if mode>=2 else 16;result=[sum(abs(x-y) for x,y in zip(data[1][j:j+8],data[2][j:j+8])) for j in range(0,n,8)];result+=[0]*(4-len(result));expected.append(result+[v for row in vectors[1:] for v in row]+[word,control,flags]+vectors[2])
request=''.join(requests);(out/'requests.txt').write_text(request);source=R/'tests/vsadbw_oracle.c';exe=out/'oracle-x86';subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True);run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,check=True,timeout=120);(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for i,(reply,want) in enumerate(zip(replies,expected)):assert [int(x,16) for x in reply.split()]==want,(i,reply,want)
r={'pass':True,'state':'finished','cases':len(expected),'modes':4,'all_unsigned_byte_pairs_per_mode':65536,'mixed_lane_cases_per_mode':1024,'source_sha256':sha(source),'verifier_sha256':sha(pathlib.Path(__file__)),'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'binary_sha256':sha(exe),'scope':'Canonical128/256 REG/MEM Rosetta VPSADBW versus Python unsigned byte sums; all65536 repeated-byte pairs plus mixed random groups; full3YMM/MXCSR/selectedflags/memory. Separate corpus from native aliases; not physical Intel/full DLL/gameplay proof.'};(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: %d canonical VPSADBW reference cases'%len(expected))
