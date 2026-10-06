#!/usr/bin/env python3
"""Freeze the reached native-system query binding, service tests and continuation."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/native-system-info-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
files=[];seen=set()
def add(p,rel):
 if rel in seen:return
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if not dst.exists():subprocess.run(['cp','-c',str(p),str(dst)],check=True)
 assert sha(p)==sha(dst),rel
 seen.add(rel);files.append(dict(original=str(p.relative_to(R)),path=rel,sha256=sha(dst)))
def tree(p,rel):
 for f in sorted(p.rglob('*')):
  if f.is_file():add(f,str(pathlib.Path(rel)/f.relative_to(p)))
root=R/'generated/native-win32/system-info-abi'
for variant in ['macos','B8138F85-0AE9-4494-9564-EDC7FD4E3B7F']:
 tests=root/variant;proof=json.loads((tests/'result.json').read_text());assert proof['pass']
 assert proof['bindings_sha256']==sha(R/'port/llasm-runtime/win32-bindings.json')
 for name,h in proof['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name,'sources/'+name)
 for row in proof['runs']:assert row['pass'] and sha(tests/('test-'+row['mode']))==row['binary_sha256']
 if variant=='macos':
  assert sha(tests/'guarded/result.json')==proof['guarded_sha256']
  assert sha(tests/'checked/result.json')==proof['checked_sha256']
 tree(tests,'tests/'+variant)
for platform in ['macos','device','simulator']:
 build_root=R/'generated/native-win32'/platform;build=json.loads((build_root/'result.json').read_text())
 assert build['bindings_sha256']==sha(R/'port/llasm-runtime/win32-bindings.json')
 assert build['objects']['guest_system']['source_sha256']==sha(R/'port/core/guest_system.c')
 for name in ['guest_system','bindings']:
  assert sha(build_root/(name+'.o'))==build['objects'][name]['object_sha256'];add(build_root/(name+'.o'),'platforms/'+platform+'/'+name+'.o')
 add(build_root/'result.json','platforms/'+platform+'/result.json')
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
run=json.loads((native/'result.json').read_text());assert run['state']=='finished' and run['initialized'] and run['clock_patch_publication'] and run['result']['engine_entered'] and not run['playable']
assert sha(native/'bootstrap')==run['binary_sha256'] and sha(native/'guarded/result.json')==run['external_build_sha256']
assert sha(R/'generated/native-win32/macos/result.json')==run['runtime_build_sha256']
checked=R/'generated/native-win32/checked-macos/result.json';assert sha(checked)==run['checked_runtime_build_sha256'];add(checked,'runtime/checked.json')
for p,h in run['source_sha256'].items():assert sha(R/p)==h,p;add(R/p,'sources/'+p)
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name,'continuation/'+name)
for p in sorted(native.glob('*.ll')):add(p,'continuation/llvm/'+p.name)
for key,items in run.items():
 if not key.endswith(('_checkpoint','_final')) or not isinstance(items,dict):continue
 for name,row in items.items():
  if isinstance(row,dict) and 'path' in row and 'sha256' in row:
   assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'continuation/capture/'+name)
for p in ['port/llasm-runtime/win32-bindings.json','scripts/freeze-native-system-info.py','scripts/build-native-win32.py','scripts/build-checked-runtime.py']:add(R/p,'sources/'+p)
add(R/'generated/integration-review/condition-variables-checkpoint-20261003/checkpoint.json','dependencies/preceding-import.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_exit=run['exit'],continuation_result=run['result'],scope='GetNativeSystemInfo uses the existing x86-only compatibility profile, real native CPU count and guest VM geometry. Both emitted API wrappers tested with full CPU/stack/error state, four protected output faults, host normal/TSan/guarded and one iPad Simulator execution. Three platform builds and actual original startup continuation. No Apple hardware architecture exposure, guest thread creation, visible original game, physical-device or authenticated-match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen',len(files),'files;',run['result'])
