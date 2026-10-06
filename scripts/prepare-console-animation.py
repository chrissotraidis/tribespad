#!/usr/bin/env python3
"""Prepare both animation CALL variants from the captured original patch suffix."""
import hashlib,json,pathlib,re,struct
import capstone
from console_animation_inputs import R,FROZEN,OUT,sha,definition,load,replay
cpu,pages,read,engine,dll=load();OUT.mkdir(parents=True,exist_ok=True)
trace,final,memory,protections=replay(cpu,pages)
(OUT/'reference.json').write_text(json.dumps(trace,indent=2)+'\n')
s=engine.read_text();preamble=s[:s.index('define ')];variants={};records=[]
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
for name,pc,oldtarget,target,w in zip(['loc_67CC8F','loc_67D8FC'],[0x67cc94,0x67d902],[0x67cfb0,0x6ba2a0],[0x10002450,0x100023f0],trace['writes']):
    entry=int(name[4:],16);raw=read(entry,pc+5-entry);original=list(decoder.disasm(raw,entry))
    assert original[-1].address==pc and original[-1].mnemonic=='call' and original[-1].size==5 and original[-1].operands[0].imm==oldtarget
    assert raw[-4:].hex()==w['before']
    patched=raw[:-4]+bytes.fromhex(w['after']);after=list(decoder.disasm(patched,entry))
    assert [(i.address,i.bytes) for i in original[:-1]]==[(i.address,i.bytes) for i in after[:-1]] and after[-1].operands[0].imm==target
    labels=[int(n,16) for n in re.findall(r'^define (?:hidden|protected) fastcc void @loc_([0-9A-F]+)\(',s,re.M) if entry<=int(n,16)<pc+5]
    assert labels==[entry] and s.count('store i32 %d,'%(pc+5))==1
    old=definition(s,name);oldname='loc_%X'%oldtarget;newname='loc_%X'%target
    assert old.count('@'+oldname+'(')==1 and old.count(';tcall '+oldname)==1
    new=old.replace('@'+oldname+'(','@'+newname+'(').replace(';tcall '+oldname,';tcall '+newname)
    variants[name]=new
    records.append(dict(name=name,call_pc=pc,old_target=oldtarget,target=target,before_bytes=raw.hex(),after_bytes=patched.hex(),old_definition_sha256=hashlib.sha256(old.encode()).hexdigest(),new_definition_sha256=hashlib.sha256(new.encode()).hexdigest(),write=w))
(OUT/'engine.ll').write_text(preamble+''.join('declare hidden fastcc void @loc_%X(%%_cpu*)\n'%r['target'] for r in records)+'\n'.join(variants.values()))
text=dll.read_text();targets={}
for n in re.findall(r'^define hidden fastcc void @(loc_[0-9A-F]+)\(',text,re.M):
    if 0x100023f0<=int(n[4:],16)<0x100024eb:targets[n]=definition(text,n)
assert len(targets)==15
(OUT/'targets.ll').write_text(text[:text.index('define ')]+'\n'.join(targets.values()))
report=dict(passed=True,published=False,records=records,target_definitions={n:hashlib.sha256(b.encode()).hexdigest() for n,b in targets.items()},
    inputs_sha256={str(p.relative_to(R)):sha(p) for p in [engine,dll,FROZEN/'checkpoint.json',R/'scripts/console_animation_inputs.py',pathlib.Path(__file__).resolve()]},
    outputs_sha256={n:sha(OUT/n) for n in ['engine.ll','targets.ll','reference.json']},
    scope='Two complete engine CALL blocks; original target routines comprise15 native blocks. Original suffix contains two displacement stores and one bounded VirtualProtect between them, with no engine call between stores. Candidate remains unpublished; no complete console initializer claim.')
(OUT/'preparation.json').write_text(json.dumps(report,indent=2)+'\n')
print('Prepared two CALL variants and15 target blocks from',len(trace['instructions']),'original instructions and one VirtualProtect')
