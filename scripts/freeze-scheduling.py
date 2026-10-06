#!/usr/bin/env python3
"""Preserve native scheduling semantics, guarded calls and startup continuation."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/scheduling-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
files=[]
def add(src,rel):
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if not dst.exists():subprocess.run(['cp','-c',str(src),str(dst)],check=True)
 assert sha(src)==sha(dst),rel
 files.append(dict(original=str(src.relative_to(R)),path=rel,sha256=sha(dst)))
def tree(src,rel):
 for p in sorted(src.rglob('*')):
  if p.is_file():add(p,str(pathlib.Path(rel)/p.relative_to(src)))
for platform in ['macos','simulator']:
 p=R/'generated/integration-review/scheduling'/platform;d=json.loads((p/'result.json').read_text());assert d['passed']
 for n,h in d['sources_sha256'].items():assert sha(R/n)==h,n
 for mode in (['checked','normal','tsan'] if platform=='macos' else ['checked']):assert d[mode]['passed'] and sha(p/('test-'+mode))==d[mode]['binary_sha256']
 assert sha(p/'checked/result.json')==d['checked_sha256'] and sha(p/'guarded/result.json')==d['guarded_sha256']
 tree(p,'tests/'+platform)
p=R/'generated/integration-review/external-memory-pass';d=json.loads((p/'result.json').read_text());assert d['pass_'];tree(p,'tests/external-regression')
for platform in ['macos','device','simulator']:
 p=R/'generated/native-win32'/platform;d=json.loads((p/'result.json').read_text())
 assert d['objects']['guest_scheduling']['source_sha256']==sha(R/'port/core/guest_scheduling.c')
 for n in ['result.json','guest_scheduling.o','bindings.ll','bindings.o']:add(p/n,'builds/'+platform+'/'+n)
p=R/'generated/native-win32/checked-macos';d=json.loads((p/'result.json').read_text());assert d['state']=='finished' and not d['failures']
for n in ['result.json','guest_scheduling.ll','guest_scheduling.bc','guest_scheduling.o']:add(p/n,'builds/checked/'+n)
p=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((p/'result.json').read_text())
assert run['state']=='finished' and run['exit']==71 and run['result']['engine_entered'] and run['result']['attach_return']==1
assert run['result']['import']['name']=='MonitorFromWindow'
assert [(e['operation'],e['result'],e['last_error']) for e in run['scheduling_events']]==[(0,0,5),(1,1,5),(2,0,50)]
assert len(run['registry_events'])==32 and all(e['result']==0 for e in run['registry_events'])
assert run['binary_sha256']==sha(p/'bootstrap') and run['external_build_sha256']==sha(p/'guarded/result.json')
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(p/n,'continuation/'+n)
for q in sorted(p.glob('*.ll')):add(q,'continuation/llvm/'+q.name)
names=['port/core/guest_scheduling.c','port/core/guest_scheduling.h','port/llasm-runtime/win32-bindings.json','port/compiler/ExternalMemoryPass.cpp','config/runtime/scheduling.json','config/runtime/external-memory-contracts.json','scripts/test-scheduling.py','scripts/gen-win32-bindings.py','scripts/guard_checked_link.py','scripts/build-native-win32.py','scripts/build-checked-runtime.py','scripts/test-qol-native-bootstrap.py','scripts/freeze-scheduling.py','tests/scheduling_test.c','tests/scheduling_errors_test.c','tests/fixtures/scheduling_dispatch.c','tests/fixtures/native_enter.ll','tests/qol_native_bootstrap.c','docs/STATUS.md']
for n in names:add(R/n,'sources/'+n)
tree(R/'generated/tools/external-memory-pass','external-pass')
for q in sorted((R/'generated/integration-review').glob('scheduling-*.log')):add(q,'logs/'+q.name)
add(R/'generated/integration-review/sdl-cpu-checkpoint-20261003/checkpoint.json','dependencies/sdl-cpu.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation=run['result'],scope='Actual native current-process niceness, unrestricted guest group masks and explicitly unsupported strict CPU affinity. macOS and sole iPad component execution; device compilation only. Original startup reaches desktop monitor selection. No menu, rendering or multiplayer acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files')
