#!/usr/bin/env python3
"""Prepare two ordered renderer CALL groups, preserving every original prefix."""
import hashlib,json,pathlib,re
import pefile
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-redirects';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';image=R/'ref/control/Tribes2.exe';p=pefile.PE(str(image));s=engine.read_text();cpu=re.search(r'^%_cpu = type .*$',s,re.M)[0]
evidence=R/'generated/integration-review/renderer-startup/execution/next-calls.json';observed=json.loads(evidence.read_text());assert observed['observed_stop']=='0x10032cbf'
variants=[];tails=[];records=[];writes=[]
rows=[('loc_446850',0x446875,0x10032cbf,0x446740,0x10031e30,0),('loc_460749',0x460814,0x10032d12,0x446740,0x10031e30,0),('loc_4657DF',0x4657fa,0x10032d60,0x446740,0x10031e30,0),('loc_5AE6F8',0x5ae70f,0x10047514,0x44bfa0,0x10052700,1),('loc_5AE770',0x5ae784,0x10047562,0x44bfa0,0x10052700,1)]
for index,(name,at,pc,old,new,group) in enumerate(rows):
 raw=p.get_data(at-0x400000,5);assert raw[0]==0xe8 and at+5+int.from_bytes(raw[1:],'little',signed=True)==old
 w=dict(pc=pc,address=at+1,size=4,before=int.from_bytes(raw[1:],'little'),value=(new-at-5)&0xffffffff)
 if index<3:
  o=observed['records'][index];assert all(int(o[k],0)==w[k] for k in ['pc','address','before','value'])
 body=definition(s,name);needle='@loc_%X('%old;assert body.count(needle)==1
 changed=body.replace(needle,'@loc_%X('%new);assert f'store i32 {at+5},' in changed;variants.append(changed)
 # Extract the exact terminal CALL IR including any live ESP/base inputs.
 tail=changed[changed.rindex(';    sub esp, esp, 4'):];tail=re.sub(r'%(\d+)',r'%v\1',tail)
 live=set(re.findall(r'%v\d+',tail))-set(re.findall(r'(%v\d+) =',tail));initial=[]
 for v in sorted(live,key=lambda x:int(x[2:])):
  original=v.replace('%v','%');line=re.search(r'^'+re.escape(original)+r' = (.*)$',body,re.M)[1]
  if 'i64' in line:
   initial.append(f'{v}_ptr = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13\n{v} = load i64, i64* {v}_ptr, align 8')
  else:
   assert line.startswith('sub i32 ');initial.append(f'{v}_ptr = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 4\n{v} = load i32, i32* {v}_ptr, align 4')
 tails.append(f'define void @tested_{index}(%_cpu* %cpu) {{\n'+'\n'.join(initial)+'\n'+tail.replace('musttail call','call'))
 records.append(dict(name=name,call=at,return_address=at+5,target=new,group=group,before_sha256=hashlib.sha256(body.encode()).hexdigest(),after_bytes=(b'\xe8'+w['value'].to_bytes(4,'little')).hex()));writes.append(w)
decls=''.join('declare hidden fastcc void @loc_%X(%%_cpu*)\n'%n for n in [0x10031e30,0x10052700]);(out/'engine.ll').write_text(s[:s.index('define ')]+'\n'+decls+''.join(variants));(out/'tails.ll').write_text(cpu+'\n'+decls+''.join(tails))
(out/'preparation.json').write_text(json.dumps(dict(passed=True,records=records,writes=writes,inputs_sha256={str(q.relative_to(R)):sha(q) for q in [engine,image,evidence,pathlib.Path(__file__).resolve()]},outputs_sha256={n:sha(out/n) for n in ['engine.ll','tails.ll']},scope='Five changed CALL targets in two publication groups (3,2); full IR prefixes identical and original return addresses retained. First group has observed runtime preimages; second has pinned-image preimages validated again before any native store.'),indent=2)+'\n');print('Prepared five CALL redirects / two publication groups')
