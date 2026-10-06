#!/usr/bin/env python3
"""Prepare the reached two dimension-source loads and six NOP bytes."""
import hashlib,json,pathlib,re
import pefile
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-dimensions';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';image=R/'ref/control/Tribes2.exe';evidence=R/'generated/integration-review/textures/execution/next-writes.json';capture=R/'generated/integration-review/textures/execution/next-stop.json'
m=json.loads(evidence.read_text());assert m['actual_stop']=='0x10047693' and m['first_protection_return']==1
writes=[{k:int(v,0) if isinstance(v,str) else v for k,v in w.items()} for w in m['writes']];assert len(writes)==6
p=pefile.PE(str(image));s=engine.read_text();records=[];variants=[];observed=json.loads(capture.read_text())['next_patch_bytes']
for name,entry,end,target,register,number in [('loc_5BAB21',0x5bab21,0x5bab53,0x5bab53,2,14),('loc_5BC3C8',0x5bc3c8,0x5bc400,0x5bc400,3,18)]:
 before=p.get_data(entry-0x400000,end-entry);data=bytearray(before)
 assert before.hex().startswith(observed[hex(entry)])
 for w in writes:
  if entry<=w['address']<end:
   at=w['address']-entry;assert int.from_bytes(data[at:at+w['size']],'little')==w['before'];data[at:at+w['size']]=w['value'].to_bytes(w['size'],'little')
 body=definition(s,name);assert body.count('zext i32 10391952 to i64')==1
 changed=body.replace('zext i32 10391952 to i64','zext i32 270362664 to i64')
 start=changed.index(';and eflags,');stop=changed.index(';load eax,',start)
 replacement=f'%{number} = add i32 %6, 0\n%dimension_register = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 {register}\nstore i32 %{number}, i32* %dimension_register, align 4\n'
 if register==2:replacement+='%dimension_flags = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 8\n%34 = load i32, i32* %dimension_flags, align 4\n'
 changed=changed[:start]+replacement+changed[stop:]
 changed=re.sub(r'%(\d+)',r'%v\1',changed);variants.append(changed)
 records.append(dict(name=name,entry=entry,end=end,target=target,before_bytes=before.hex(),after_bytes=data.hex(),before_sha256=hashlib.sha256(body.encode()).hexdigest()))
(out/'engine.ll').write_text(s[:s.index('define ')]+''.join('declare hidden fastcc void @loc_%X(%%_cpu*)\n'%r['target'] for r in records)+''.join(variants))
(out/'preparation.json').write_text(json.dumps(dict(passed=True,records=records,writes=writes,inputs_sha256={str(q.relative_to(R)):sha(q) for q in [engine,image,evidence,capture,pathlib.Path(__file__).resolve()]},outputs_sha256={'engine.ll':sha(out/'engine.ll')},scope='Two original absolute loads use DLL dimension pointer; NOPs remove ADD including its flag effects. Original suffixes and all protection branches preserved.'),indent=2)+'\n');print('Prepared two dimension-source blocks / six stores')
