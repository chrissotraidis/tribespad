#!/usr/bin/env python3
"""Preserve primary monitor ABI and the subsequent protected patch failure."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/monitor-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
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
 p=R/'generated/integration-review/monitor'/platform;d=json.loads((p/'result.json').read_text());assert d['passed']
 for n,h in d['sources_sha256'].items():assert sha(R/n)==h,n
 for mode in (['checked','normal','tsan'] if platform=='macos' else ['checked']):assert d[mode]['passed'] and sha(p/('test-'+mode))==d[mode]['binary_sha256']
 assert sha(p/'checked/result.json')==d['checked_sha256'] and sha(p/'guarded/result.json')==d['guarded_sha256']
 tree(p,'tests/'+platform)
p=R/'generated/integration-review/monitor-display-regression';d=json.loads((p/'result.json').read_text());assert d['passed'];tree(p,'tests/desktop-regression')
for platform in ['macos','device','simulator']:
 p=R/'generated/native-win32'/platform;d=json.loads((p/'result.json').read_text())
 assert d['objects']['guest_display']['source_sha256']==sha(R/'port/core/guest_display.c')
 for n in ['result.json','display.o','guest_display.o','bindings.ll','bindings.o']:add(p/n,'builds/'+platform+'/'+n)
p=R/'generated/native-win32/checked-macos';d=json.loads((p/'result.json').read_text());assert d['state']=='finished' and not d['failures']
for n in ['result.json','guest_display.ll','guest_display.bc','guest_display.o']:add(p/n,'builds/checked/'+n)
p=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((p/'result.json').read_text())
assert run['state']=='finished' and run['exit']==-6 and run['result']['engine_entered'] and run['result']['attach_return']==1
assert run['result']['kind']=='memory_violation' and '0x5d2860 size=4 access=2' in run['result']['reason']
assert run['monitor_events'][1]['result']==1 and run['monitor_events'][1]['size']==40
assert len(run['registry_events'])==32 and all(e['result']==0 for e in run['registry_events'])
assert run['binary_sha256']==sha(p/'bootstrap') and run['external_build_sha256']==sha(p/'guarded/result.json')
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(p/n,'continuation/'+n)
for q in sorted(p.glob('*.ll')):add(q,'continuation/llvm/'+q.name)
names=['port/core/guest_display.c','port/core/guest_display.h','port/apple/display.m','port/apple/display.h','port/llasm-runtime/win32-bindings.json','config/runtime/monitor.json','scripts/test-monitor.py','scripts/gen-win32-bindings.py','scripts/test-qol-native-bootstrap.py','scripts/freeze-monitor.py','tests/monitor_test.c','tests/fixtures/monitor_dispatch.c','tests/fixtures/native_enter.ll','tests/guest_display_test.c','tests/qol_native_bootstrap.c','tests/engine_bootstrap.c','docs/archive/STATUS.md']
for n in names:add(R/n,'sources/'+n)
for q in sorted((R/'generated/integration-review').glob('monitor-*.log')):add(q,'logs/'+q.name)
add(R/'generated/integration-review/scheduling-checkpoint-20261003/checkpoint.json','dependencies/scheduling.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation=run['result'],scope='Initial primary display and real work geometry through monitor A/W ABI on Mac and sole iPad Simulator. All Apple builds. Original native monitor query succeeds; next protected code write aborts. No permission relaxation or eighth revision. No visible game window, rendering or multiplayer acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files')
