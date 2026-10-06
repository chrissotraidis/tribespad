#!/usr/bin/env python3
"""Canonical scalar MIN/MAX/COMISD versus exact rational classification."""
import hashlib,json,pathlib,random,subprocess
from scalar_select_reference import evaluate
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/sse/vscalar-select-reference';out.mkdir(parents=True,exist_ok=True)
single=[0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x3f7fffff,0x3f800000,0xbf800000,0x40000000,0xc0000000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0xffc56789,0x7f812345,0xff856789]
double=[0,0x8000000000000000,1,0x8000000000000001,0x000fffffffffffff,0x800fffffffffffff,0x0010000000000000,0x8010000000000000,0x3fefffffffffffff,0x3ff0000000000000,0xbff0000000000000,0x4000000000000000,0xc000000000000000,0x7fefffffffffffff,0xffefffffffffffff,0x7ff0000000000000,0xfff0000000000000,0x7ff812345678abcd,0xfff856789abcdef0,0x7ff012345678abcd,0xfff056789abcdef0]
rng=random.Random(25034);cases=[];offsets=[];counts=[]
for op in range(3):
    values=double if op==2 else single;bits=64 if op==2 else 32
    pairs=[(a,b) for a in values for b in values]+[(rng.getrandbits(bits),rng.getrandbits(bits)) for _ in range(256)]
    offsets.append(len(cases))
    for left,right in pairs:
        for setting in range(16):
            for sticky in [0,1,31,63]:
                control=0x1f80|((setting&3)<<13)|(0x40 if setting&4 else 0)|(0x8000 if setting&8 else 0)|sticky
                result,status=evaluate(op,left,right,control);same_result,same_status=evaluate(op,right,right,control)
                cases.append((op,left,right,control,result,status,same_result,same_status))
    counts.append(len(cases)-offsets[-1])
requests=[];expected=[]
for index,(op,left,right,control,result,status,same_result,same_status) in enumerate(cases):
    for form in range(3):
        vectors=[[rng.getrandbits(64) for _ in range(4)] for _ in range(3)]
        a=right if form==2 else left;b=right
        if op==2:vectors[1][0]=a;vectors[2][0]=b
        else:vectors[1][0]=(vectors[1][0]&0xffffffff00000000)|a;vectors[2][0]=(vectors[2][0]&0xffffffff00000000)|b
        flags=[0,0x8d5,0x41,0x884][index%4];word=rng.getrandbits(32);mode=op*3+form
        request=f'{mode} {control:x} {flags:x} {word:x} '+ ' '.join(f'{v:016x}' for row in vectors for v in row)+' 0\n';requests.append(request)
        capture=list(vectors[2]);output=[row[:] for row in vectors]
        chosen=same_result if form==2 else result;raised=same_status if form==2 else status
        if op==2:after_flags=chosen
        else:
            output[0]=[chosen|(vectors[1][0]&0xffffffff00000000),vectors[1][1],0,0];after_flags=flags
        expected.append([v for row in output for v in row]+[word,control|raised,after_flags]+capture)
request=''.join(requests);(out/'requests.txt').write_text(request)
source=R/'tests/vscalar_select_oracle.c';exe=out/'oracle-x86'
subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True)
run=subprocess.run(['arch','-x86_64',str(exe)],input=request,text=True,capture_output=True,check=True,timeout=120)
(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(expected)
for index,(reply,want) in enumerate(zip(replies,expected)):
    actual=[int(x,16) for x in reply.split()];assert actual==want,(index,cases[index//3],actual,want)
table=out/'cases.inc';table.write_text('static const unsigned offsets[]={'+','.join(map(str,offsets))+'};\nstatic const unsigned counts[]={'+','.join(map(str,counts))+'};\nstatic const Case cases[]={\n'+',\n'.join('{'+str(op)+'u,'+f'UINT64_C(0x{left:x}),UINT64_C(0x{right:x}),'+','.join(f'0x{x:x}u' for x in [control,result,status,same_result,same_status])+'}' for op,left,right,control,result,status,same_result,same_status in cases)+'\n};\n')
paths=[source,R/'scripts/scalar_select_reference.py',pathlib.Path(__file__).resolve()]
r={'pass':True,'state':'finished','cases':len(cases),'canonical_cases':len(expected),'per_operation':counts,'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'requests_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'corpus_sha256':sha(table),'binary_sha256':sha(exe),'scope':'Exact rational ordered classification/selection versus canonical C5 REG/MEM/diagonal Rosetta instructions; full3YMM/flags/MXCSR/memory state. All16 RC/DAZ/FTZ and4 sticky patterns; no physical Intel/unmasked x86/actual emitted-body claim.'}
(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
