#!/usr/bin/env python3
"""Prepare the reached nine-byte NOP patch, preserving both historical entries."""
import hashlib,json,pathlib,re
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/input-nop';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
capture=R/'generated/integration-review/startup-calls/execution';cap=json.loads((capture/'capture.json').read_text())
assert cap['entry']=='0x100550b9' and cap['eax']==1 and cap['bytes']=='e865c8eaff84c07412'
assert sha(capture/'engine-page.bin')==cap['page_sha256'] and sha(capture/'cpu.bin')==cap['cpu_sha256']
raw=(capture/'engine-page.bin').read_bytes()[0x806:0x80f];assert raw.hex()==cap['bytes']
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';s=engine.read_text()
entries=[int(n,16) for n in re.findall(r'^define (?:hidden|protected) fastcc void @loc_([0-9A-F]+)\(',s,re.M) if 0x556806<=int(n,16)<0x55680f]
assert entries==[0x556806,0x55680b]
rows=[];variants=[]
for entry in entries:
 name='loc_%X'%entry;body=definition(s,name)
 variants.append('define hidden fastcc void @%s(%%_cpu* %%cpu) nounwind {\n musttail call fastcc void @loc_55680F(%%_cpu* %%cpu)\n ret void\n}\n'%name)
 rows.append(dict(name=name,entry=entry,end=0x55680f,target=0x55680f,before_bytes=raw[entry-0x556806:].hex(),after_bytes=(b'\x90'*(0x55680f-entry)).hex(),old_definition_sha256=hashlib.sha256(body.encode()).hexdigest()))
(out/'engine.ll').write_text(s[:s.index('define ')]+'declare hidden fastcc void @loc_55680F(%_cpu*)\n'+''.join(variants))
writes=[]
for pc,address,size in [(0x100550be,0x556806,4),(0x100550c4,0x55680a,4),(0x100550cb,0x55680e,1)]:
 offset=address-0x556806;writes.append(dict(pc=pc,address=address,size=size,before=int.from_bytes(raw[offset:offset+size],'little'),value=int.from_bytes(b'\x90'*size,'little')))
report=dict(passed=True,published=False,records=rows,writes=writes,inputs_sha256={str(p.relative_to(R)):sha(p) for p in [capture/'capture.json',capture/'engine-page.bin',capture/'cpu.bin',engine,pathlib.Path(__file__).resolve()]},outputs_sha256={'engine.ll':sha(out/'engine.ll')},scope='Exactly nine captured bytes become NOPs. Both original entries continue to0x55680f without modifying CPU or memory. Three ordered original stores required; no arbitrary code mutation.')
(out/'preparation.json').write_text(json.dumps(report,indent=2)+'\n');print('Prepared two NOP entries and three exact stores')
