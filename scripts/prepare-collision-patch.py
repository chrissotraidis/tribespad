#!/usr/bin/env python3
"""Prepare the complete captured caller and unchanged six-block original target."""
import hashlib,json,pathlib,re,struct
import capstone
from collision_patch_inputs import R,FROZEN,OUT,sha,definition,load,replay
cpu,pages,read,engine,dll=load();OUT.mkdir(exist_ok=True)
trace,final,memory,permissions=replay(cpu,pages)
(OUT/'reference.json').write_text(json.dumps(trace,indent=2)+'\n')
name='loc_617BA0';entry=0x617ba0;pc=0x617c02;target=0x10009900;oldtarget=0x58d900
raw=read(entry,pc+5-entry);decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
before=list(decoder.disasm(raw,entry));assert len(before)==22 and before[-1].address==pc and before[-1].mnemonic=='call' and before[-1].operands[0].imm==oldtarget
w=trace['writes'][0];assert raw[-4:].hex()==w['before']
patched=raw[:-4]+bytes.fromhex(w['after']);after=list(decoder.disasm(patched,entry));assert [i.bytes for i in after[:-1]]==[i.bytes for i in before[:-1]] and after[-1].operands[0].imm==target
s=engine.read_text();assert [n for n in re.findall(r'^define hidden fastcc void @(loc_[0-9A-F]+)\(',s,re.M) if entry<=int(n[4:],16)<pc+5]==[name]
old=definition(s,name);assert old.count('@loc_58D900(')==old.count(';tcall loc_58D900')==1
new=old.replace('@loc_58D900(','@loc_10009900(').replace(';tcall loc_58D900',';tcall loc_10009900')
(OUT/'engine.ll').write_text(s[:s.index('define ')]+'declare hidden fastcc void @loc_10009900(%_cpu*)\n'+new)
text=dll.read_text();targets={n:definition(text,n) for n in re.findall(r'^define hidden fastcc void @(loc_[0-9A-F]+)\(',text,re.M) if 0x10009900<=int(n[4:],16)<0x10009aa7};assert len(targets)==6
assert all('halopad_trap_unimplemented' not in body for body in targets.values())
(OUT/'targets.ll').write_text(text[:text.index('define ')]+'\n'.join(targets.values()))
record=dict(name=name,call_pc=pc,old_target=oldtarget,target=target,before_bytes=raw.hex(),after_bytes=patched.hex(),old_definition_sha256=hashlib.sha256(old.encode()).hexdigest(),new_definition_sha256=hashlib.sha256(new.encode()).hexdigest(),write=w)
report=dict(passed=True,published=False,records=[record],target_definitions={n:hashlib.sha256(b.encode()).hexdigest() for n,b in targets.items()},
            inputs_sha256={str(p.relative_to(R)):sha(p) for p in [engine,dll,FROZEN/'checkpoint.json',R/'scripts/collision_patch_inputs.py',pathlib.Path(__file__).resolve()]},
            outputs_sha256={n:sha(OUT/n) for n in ['engine.ll','targets.ll','reference.json']},
            scope='One complete 22-instruction caller and the six-block original collision-query target with coordinate transform. One displacement store follows successful VirtualProtect. Candidate only; no eleventh runtime publication or behavioral acceptance.')
(OUT/'preparation.json').write_text(json.dumps(report,indent=2)+'\n')
print('Prepared one complete CALL variant and six original target blocks')
