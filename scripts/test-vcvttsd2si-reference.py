#!/usr/bin/env python3
"""Check canonical x86 W0 conversion versus exact integer-division expectations."""
import hashlib,json,pathlib,random,subprocess
from f64_truncate_reference import convert
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/sse/vcvttsd2si-reference';out.mkdir(parents=True,exist_ok=True)
values={0,0x8000000000000000,1,0x8000000000000001,0x000fffffffffffff,0x800fffffffffffff,0x7ff0000000000000,0xfff0000000000000,0x7ff800001234abcd,0xfff800001234abcd,0x7ff000001234abcd,0xfff000001234abcd}
for exponent in [0,1,2046,2047]+list(range(1019,1058)):
    for fraction in [0,1,0x0008000000000000,0x000ffffffffffffe,0x000fffffffffffff]:
        for sign in [0,1]:values.add(sign<<63|exponent<<52|fraction)
# ULP neighbours of the signed32 limits and values one unit beyond them.
for center in [0x41dfffffffc00000,0x41e0000000000000,0x41e0000000200000]:
    for delta in range(-17,18):
        for sign in [0,1]:values.add((center+delta)|(sign<<63))
rng=random.Random(25034);values.update(rng.getrandbits(64) for _ in range(1024))
cases=[]
for source in sorted(values):
    for bits in range(16):
        for sticky in [0,1,31,63]:
            control=0x1f80|((bits&3)<<13)|(0x40 if bits&4 else 0)|(0x8000 if bits&8 else 0)|sticky
            result,status=convert(source,control);cases.append((source,control,result,status))
requests=''.join(f'{mode} {source:016x} {control:04x} {([0,0x8d5,0x41,0x884][i%4]):03x}\n' for mode in range(2) for i,(source,control,_,_) in enumerate(cases))
(out/'requests.txt').write_text(requests);oracle_source=R/'tests/vcvttsd2si_oracle.c';oracle=out/'oracle-x86'
subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(oracle_source),'-o',str(oracle)],check=True)
run=subprocess.run(['arch','-x86_64',str(oracle)],input=requests,text=True,capture_output=True,check=True,timeout=120)
(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(cases)*2
for index,reply in enumerate(replies):
    i=index%len(cases);source,control,result,status=cases[i];actual=[int(x,16) for x in reply.split()]
    assert actual==[result,control|status,[0,0x8d5,0x41,0x884][i%4],source,0x123456789abcdef0,0xfedcba9876543210,0x55555555aaaaaaaa],(index,cases[i],reply)
table=out/'cases.inc';table.write_text('static const Case cases[]={\n'+',\n'.join('{'+f'UINT64_C(0x{source:x}),0x{control:x}u,0x{result:x}u,0x{status:x}u'+'}' for source,control,result,status in cases)+'\n};\n')
paths=[oracle_source,R/'scripts/f64_truncate_reference.py',pathlib.Path(__file__).resolve()]
report={'state':'finished','pass':True,'cases':len(cases),'canonical_cases':len(replies),'input_values':len(values),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'request_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'corpus_sha256':sha(table),'binary_sha256':sha(oracle),'scope':'Exact integer division/truncation classification versus canonical W0 register and unaligned memory Rosetta instructions; all16 RC/DAZ/FTZ and4 sticky patterns, vector/flags/memory preserved. W1 IA-32 behavior follows SDM; no physical Intel, unmasked x86 faults or native emitted proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
