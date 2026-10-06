#!/usr/bin/env python3
"""Prepare three unpublished tail variants from the original native preimages."""
import hashlib,json,pathlib,re
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen=R/'generated/integration-review/post-video-nop-second-checkpoint-20261003';source=frozen/'generated/integration-review/post-video-tail/analysis.json';out=R/'generated/integration-review/post-video-tail-candidate';out.mkdir(exist_ok=True)
checkpoint=json.loads((frozen/'checkpoint.json').read_text());assert sha(source)==checkpoint['files'][str(source.relative_to(frozen))];meta=json.loads(source.read_text());assert meta['passed'] and not meta['published'] and len(meta['records'])==3
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';s=engine.read_text();variants=[];records=[]
for row in meta['records']:
 row=dict(row);name=row['name'];body=definition(s,name)
 if name=='loc_4023F0':
  assert body.count('@loc_4032F0(')==1;changed=body.replace('@loc_4032F0(','@loc_1005AF70(').replace(';tcall loc_4032F0',';tcall loc_1005AF70')
 elif name=='loc_4B3C70':
  assert body.count('store i32 72, i32* %57, align 4')==1;changed=body.replace('store i32 72, i32* %57, align 4','store i32 84, i32* %57, align 4').replace(';    store 0x48, esp, 4',';    store 0x54, esp, 4')
 else:
  assert name=='loc_4C65D0' and body.count('store i32 0, i32* %20, align 4')==1
  changed=body.replace('store i32 0, i32* %20, align 4','store i32 2, i32* %20, align 4')
  at=changed.index('%17 = sub i32');changed=changed[:at]+changed[at:].replace(';    store 0x0, esp, 4',';    store 0x2, esp, 4',1)
 assert changed!=body;variants.append(changed);row['old_definition_sha256']=hashlib.sha256(body.encode()).hexdigest();row['new_definition_sha256']=hashlib.sha256(changed.encode()).hexdigest();records.append(row)
pre=s[:s.index('define ')];declarations='\n'.join('declare hidden fastcc void @loc_%X(%%_cpu*)'%r['target'] for r in records);(out/'engine.ll').write_text(pre+declarations+'\n'+''.join(variants))
report=dict(passed=True,published=False,records=records,inputs_sha256={str(p.relative_to(R)):sha(p) for p in [engine,source,frozen/'checkpoint.json',pathlib.Path(__file__).resolve()]},outputs_sha256={'engine.ll':sha(out/'engine.ll')},scope='Three conditional candidates from reached native preimages: CALL target and two PUSH immediates. Only the CALL writer has been reached. No native write, protection result, publication or new callback acceptance.')
(out/'preparation.json').write_text(json.dumps(report,indent=2)+'\n');print('Prepared three unpublished post-video tail variants')
