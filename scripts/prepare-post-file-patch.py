#!/usr/bin/env python3
"""Prepare the complete captured caller and unchanged 21-block original target."""
import hashlib,json,pathlib,re,struct
import capstone
from post_file_patch_inputs import R,FROZEN,OUT,sha,definition,load,replay
cpu,pages,read,engine,dll=load();OUT.mkdir(exist_ok=True)
trace,final,memory,permissions=replay(cpu,pages)
(OUT/'reference.json').write_text(json.dumps(trace,indent=2)+'\n')
name='loc_5BFF77';entry=0x5bff77;pc=0x5bff78;target=0x10015a10;oldtarget=0x4267a0
raw=read(entry,6);decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
before=list(decoder.disasm(raw,entry));assert len(before)==2 and before[-1].address==pc and before[-1].mnemonic=='call' and before[-1].operands[0].imm==oldtarget
w=trace['writes'][0];assert raw[-4:].hex()==w['before']
patched=raw[:-4]+bytes.fromhex(w['after']);after=list(decoder.disasm(patched,entry));assert after[0].bytes==before[0].bytes and after[-1].operands[0].imm==target
s=engine.read_text();assert [n for n in re.findall(r'^define hidden fastcc void @(loc_[0-9A-F]+)\(',s,re.M) if entry<=int(n[4:],16)<pc+5]==[name]
old=definition(s,name);assert old.count('@loc_4267A0(')==old.count(';tcall loc_4267A0')==1
new=old.replace('@loc_4267A0(','@loc_10015A10(').replace(';tcall loc_4267A0',';tcall loc_10015A10')
(OUT/'engine.ll').write_text(s[:s.index('define ')]+'declare hidden fastcc void @loc_10015A10(%_cpu*)\n'+new)
text=dll.read_text();targets={n:definition(text,n) for n in re.findall(r'^define hidden fastcc void @(loc_[0-9A-F]+)\(',text,re.M) if 0x10015a10<=int(n[4:],16)<0x10015b1c};assert len(targets)==21
assert all('halopad_trap_unimplemented' not in body for body in targets.values())
(OUT/'targets.ll').write_text(text[:text.index('define ')]+'\n'.join(targets.values()))
record=dict(name=name,call_pc=pc,old_target=oldtarget,target=target,before_bytes=raw.hex(),after_bytes=patched.hex(),old_definition_sha256=hashlib.sha256(old.encode()).hexdigest(),new_definition_sha256=hashlib.sha256(new.encode()).hexdigest(),write=w)
report=dict(passed=True,published=False,records=[record],target_definitions={n:hashlib.sha256(b.encode()).hexdigest() for n,b in targets.items()},
            inputs_sha256={str(p.relative_to(R)):sha(p) for p in [engine,dll,FROZEN/'checkpoint.json',R/'scripts/post_file_patch_inputs.py',pathlib.Path(__file__).resolve()]},
            outputs_sha256={n:sha(OUT/n) for n in ['engine.ll','targets.ll','reference.json']},
            scope='One complete two-instruction caller and the21-block original serverCmd/onEarlyCommand target. One displacement store follows successful VirtualProtect. Candidate only; no tenth runtime publication or behavioral acceptance.')
(OUT/'preparation.json').write_text(json.dumps(report,indent=2)+'\n')
print('Prepared one complete CALL variant and21 original target blocks')
