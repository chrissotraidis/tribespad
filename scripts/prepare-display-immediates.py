#!/usr/bin/env python3
"""Prepare AOT MOV variants reading finite, protected guest immediate data."""
import hashlib,json,pathlib
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen=R/'generated/integration-review/sdl-display-checkpoint-20261003';root=frozen/'generated/integration-review/display-immediates';out=R/'generated/integration-review/display-immediates-candidate';out.mkdir(exist_ok=True)
checkpoint=json.loads((frozen/'checkpoint.json').read_text());cap=json.loads((root/'capture.json').read_text())
for p in [root/'capture.json',root/'00561000.bin',root/'cpu.bin',root/'mode.bin']:assert sha(p)==checkpoint['files'][str(p.relative_to(frozen))]
assert cap['pc']=='0x1005b5ac';page=(root/'00561000.bin').read_bytes();engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';s=engine.read_text();records=[];variants=[]
for entry,before,writer,target in [(0x561250,800,0x1005b5ac,0x561255),(0x561272,600,0x1005b5cd,0x561277)]:
 name='loc_%X'%entry;body=definition(s,name);raw=page[entry-0x561000:entry-0x561000+5];assert raw==b'\xb8'+before.to_bytes(4,'little');needle='store i32 %d, i32* %%1, align 4'%before;assert body.count(needle)==1
 change='''%mode_offset = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13
%mode_base = load i64, i64* %mode_offset, align 8
%mode_address = add i64 %mode_base, ADDRESS
%mode_pointer = inttoptr i64 %mode_address to i32*
%mode_value = load i32, i32* %mode_pointer, align 1
store i32 %mode_value, i32* %1, align 4'''.replace('ADDRESS',str(entry+1))
 changed=body.replace(needle,change).replace(';mov eax, 0x%x'%before,';mov eax, validated display immediate');variants.append(changed)
 records.append(dict(name=name,entry=entry,address=entry+1,size=4,before=before,writer=writer,target=target,bytes=raw.hex(),old_definition_sha256=hashlib.sha256(body.encode()).hexdigest(),new_definition_sha256=hashlib.sha256(changed.encode()).hexdigest()))
(out/'engine.ll').write_text(s[:s.index('define ')]+'\n'.join('declare hidden fastcc void @loc_%X(%%_cpu*)'%r['target'] for r in records)+'\n'+''.join(variants))
report=dict(passed=True,published=False,records=records,inputs_sha256={str(p.relative_to(R)):sha(p) for p in [frozen/'checkpoint.json',root/'capture.json',root/'00561000.bin',engine,pathlib.Path(__file__).resolve()]},outputs_sha256={'engine.ll':sha(out/'engine.ll')},scope='Two precompiled MOV blocks load the exact guest immediate bytes as checked data. No per-device constants baked into code, no JIT. Width writer captured, height writer not yet executed. Publication must guard the finite ordered stores and keep these routes unavailable during a partial revision.')
(out/'preparation.json').write_text(json.dumps(report,indent=2)+'\n');print('Prepared two unpublished dynamic display MOV variants')
