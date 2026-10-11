#!/usr/bin/env python3
"""Freeze source-bound SDL component and original-engine display evidence."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-display-checkpoint-20261003';assert not (out/'checkpoint.json').exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();files={}
def add(p):
 p=p.resolve();rel=str(p.relative_to(R))
 if rel in files:return
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);subprocess.run(['cp','-c',str(p),str(dst)],check=True);assert sha(p)==sha(dst);files[rel]=sha(dst)
def sources(d,key):
 for n,h in d[key].items():assert sha(R/n)==h,n;add(R/n)
def tree(p):
 for f in p.rglob('*'):
  if f.is_file() and '__pycache__' not in f.parts:add(f)
root=R/'generated/integration-review/sdl-display'
for name in ['macos','B8138F85-0AE9-4494-9564-EDC7FD4E3B7F','layout']:
 p=root/name/'result.json';d=json.loads(p.read_text());assert d['passed'];sources(d,'sources_sha256');add(p)
 for row in d.get('runs',[]):
  app=R/row['app'];exe=app/'Contents/MacOS/SDLDisplayProbe' if name=='macos' else app/'SDLDisplayProbe';assert sha(exe)==row['binary_sha256'];tree(app)
proof=json.loads((root/'execution/verification.json').read_text());assert proof['passed'];sources(proof,'sources_sha256');tree(root/'execution');tree(root/'layout')
n=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';d=json.loads((n/'result.json').read_text());assert d['exit']==-6 and d['initialized'] and d['result']['kind']=='memory_violation' and d['result']['reason']=='at 0x561251 size=4 access=2';assert sha(n/'bootstrap')==d['binary_sha256']==proof['binary_sha256'];sources(d,'source_sha256')
for f in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','engine-routed.ll','qol-patched.ll','engine.bin','qol.bin','guarded/result.json','guarded/policy.json','support/result.json']:add(n/f)
g=json.loads((n/'guarded/result.json').read_text());assert g['plugin_build_sha256']==sha(R/'generated/tools/external-memory-pass/result.json') and g['config_sha256']==sha(R/'config/runtime/external-memory-contracts.json')
tree(R/'generated/integration-review/display-immediates')
for f in ['config/runtime/sdl-display.json','config/runtime/sdl-bindings.json','config/runtime/external-memory-contracts.json','port/core/guest_sdl_backend.h','scripts/freeze-sdl-display.py','scripts/test-sdl-display.py','scripts/test-sdl-display-layout.py','docs/archive/STATUS.md','docs/archive/EXECUTION-PLAN.md','generated/tools/external-memory-pass/result.json','generated/integration-review/post-video-tail-checkpoint-20261003/checkpoint.json']:add(R/f)
report=dict(passed=True,goal_complete=False,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,binary_sha256=d['binary_sha256'],continuation_result=d['result'],scope='Real Cocoa/UIKit display service component tests and original two-query native/guest value and ABI correspondence. Ordinary full startup advances to width immediate. No window, HD, gameplay or authenticated multiplayer.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(files),'source-verified files frozen')
