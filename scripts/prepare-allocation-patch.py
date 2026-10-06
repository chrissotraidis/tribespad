#!/usr/bin/env python3
"""Prepare the complete original allocation block with its observed new size."""
import hashlib,json,pathlib,capstone
from allocation_patch_inputs import R,FROZEN,OUT,sha,definition,load,replay
cpu,pages,read,engine,dll=load();OUT.mkdir(exist_ok=True)
trace,final,memory,permissions=replay(cpu,pages)
w=trace['writes'][0]
assert (w['pc'],w['address'],w['size'],w['before'],w['after'])==(0x1000c433,0x615f14,4,'ac0e0000','2c0f0000')
(OUT/'reference.json').write_text(json.dumps(trace,indent=2)+'\n')
entry=0x615f00;name='loc_615F00';raw=read(entry,0x1d)
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
before=list(decoder.disasm(raw,entry))
instruction=next(i for i in before if i.address==0x615f13)
assert instruction.mnemonic=='push' and instruction.bytes.hex()=='68ac0e0000'
assert before[-1].address==0x615f18 and before[-1].mnemonic=='call' and before[-1].operands[0].imm==0x401fe0
patched=raw[:0x14]+bytes.fromhex(w['after'])+raw[0x18:]
after=list(decoder.disasm(patched,entry))
assert len(before)==len(after)
for a,b in zip(before,after):
    if a.address==0x615f13:assert b.mnemonic=='push' and b.operands[0].imm==0xf2c
    else:assert a.bytes==b.bytes
s=engine.read_text();old=definition(s,name)
assert old.count('store i32 3756,')==1 and old.count(';    store 0xeac, esp, 4')==1
new=old.replace('store i32 3756,','store i32 3884,').replace(';    store 0xeac, esp, 4',';    store 0xf2c, esp, 4')
(OUT/'engine.ll').write_text(s[:s.index('define ')]+'declare hidden fastcc void @loc_401FE0(%_cpu*)\n'+new)
record=dict(name=name,instruction=0x615f13,old_size=3756,new_size=3884,before_bytes=raw.hex(),after_bytes=patched.hex(),old_definition_sha256=hashlib.sha256(old.encode()).hexdigest(),new_definition_sha256=hashlib.sha256(new.encode()).hexdigest(),write=w)
report=dict(passed=True,published=False,records=[record],target_definitions={},instruction_count=len(before),
    inputs_sha256={str(p.relative_to(R)):sha(p) for p in [engine,dll,FROZEN/'checkpoint.json',R/'scripts/allocation_patch_inputs.py',pathlib.Path(__file__).resolve()]},
    outputs_sha256={n:sha(OUT/n) for n in ['engine.ll','reference.json']},
    scope='One complete allocation block; only the observed PUSH size changes from3756 to3884. Existing allocator call and return address retained. No twelfth publication or allocation-lifetime claim.')
(OUT/'preparation.json').write_text(json.dumps(report,indent=2)+'\n')
print('Prepared allocation-size variant;',len(before),'original instructions')
