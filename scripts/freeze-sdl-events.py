#!/usr/bin/env python3
"""Preserve checked SDL event lifecycle and original startup continuation."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-events-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();files={}
def add(p):
 p=p.resolve();rel=p.relative_to(R);dst=out/rel
 if str(rel) in files:return
 dst.parent.mkdir(parents=True,exist_ok=True);subprocess.run(['cp','-c',str(p),str(dst)],check=True);assert sha(p)==sha(dst);files[str(rel)]=sha(dst)
for suffix in ['', '/B8138F85-0AE9-4494-9564-EDC7FD4E3B7F']:
 root=R/('generated/integration-review/sdl-hints'+suffix);x=json.loads((root/'result.json').read_text());assert x['passed'];add(root/'result.json')
 for name,h in x['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name)
 for mode in ['normal','tsan','checked']:
  if mode in x:
   assert sha(root/('test-'+mode))==x[mode]['binary_sha256'];add(root/('test-'+mode));add(root/(mode+'.log'))
 assert sha(root/'checked/result.json')==x['checked_sha256'];assert sha(root/'guarded/result.json')==x['guarded_sha256']
 add(root/'checked/result.json');add(root/'guarded/result.json');add(root/'guarded/policy.json')
root=R/'generated/integration-review/sdl-events';platforms=json.loads((root/'platforms.json').read_text());assert platforms['passed'];add(root/'platforms.json')
for platform,row in platforms['platforms'].items():assert row['source_sha256']==sha(R/'port/core/guest_sdl_backend.c');assert row['object_sha256']==sha(root/(platform+'.o'));add(root/(platform+'.o'))
add(root/'file-query.json');add(root/'file-attributes-lldb.log')
native=R/'generated/srw/run-20261003T150627-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text());assert run['state']=='finished' and run['initialized'] and sha(native/'bootstrap')==run['binary_sha256']
for name,h in run['source_sha256'].items():assert sha(R/name)==h,name;add(R/name)
for name in ['bootstrap','engine.bin','qol.bin','result.json','stderr.log','stdout.log','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name)
for name in ['config/runtime/sdl-events.json','scripts/freeze-sdl-events.py','generated/integration-review/sdl-logging-checkpoint-20261003/checkpoint.json','generated/tools/external-memory-pass/ExternalMemoryPass.dylib','generated/tools/external-memory-pass/result.json','generated/integration-review/external-memory-pass/result.json','generated/integration-review/external-memory-pass/run.log','docs/archive/STATUS.md','docs/archive/EXECUTION-PLAN.md']:add(R/name)
external=json.loads((R/'generated/integration-review/external-memory-pass/result.json').read_text());assert external['pass_']
for name,h in external['plugins'].items():assert sha(R/name)==h;add(R/name)
(out/'checkpoint.json').write_text(json.dumps(dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_exit=run['exit'],continuation_result=run['result'],scope='Native SDL EVENTS initialization, failure/retry and reference-counted lifecycle bridges. Mac normal/TSan/both guards, sole guarded iPad, production mobile compilation, original startup continuation. No original-game window/map/HD/controls or authenticated match acceptance.'),indent=2)+'\n')
for name,h in files.items():assert sha(out/name)==h
print('PASS:',len(files),'source-verified files frozen')
