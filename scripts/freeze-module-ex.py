#!/usr/bin/env python3
"""Freeze the retained mapped-module query binding, service tests and continuation."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/module-ex-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
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
root=R/'generated/integration-review/module-ex'
for variant in ['macos','B8138F85-0AE9-4494-9564-EDC7FD4E3B7F']:
 tests=root/variant;proof=json.loads((tests/'result.json').read_text());assert proof['passed']
 for name,h in proof['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name,'sources/'+name)
 for row in proof['runs']:assert row['passed'] and sha(tests/('test-'+row['mode']))==row['binary_sha256']
 if variant=='macos':
  assert sha(tests/'guarded/result.json')==proof['guarded_sha256']
  assert sha(tests/'checked/result.json')==proof['checked_sha256']
 tree(tests,'tests/'+variant)
analysis=json.loads((root/'thread-request.json').read_text());assert analysis['passed']
for p,h in analysis['inputs_sha256'].items():assert sha(R/p)==h,p
add(root/'thread-request.json','analysis/thread-request.json')
add(R/'scripts/analyze-thread-create.py','sources/scripts/analyze-thread-create.py')
for name in ['module-regression','module-regression.log']:add(root/name,'tests/'+name)
assert (root/'module-regression.log').read_text().startswith('PASS:')
add(R/'tests/guest_modules_test.c','sources/tests/guest_modules_test.c')
regression=R/'generated/native-win32/module-lookup-abi/macos'
r=json.loads((regression/'result.json').read_text());assert r['pass'] and r['bindings_sha256']==sha(R/'port/llasm-runtime/win32-bindings.json')
for p,h in r['sources_sha256'].items():assert sha(R/p)==h;add(R/p,'sources/'+p)
for row in r['runs']:assert row['pass'] and sha(regression/row['mode'])==row['binary_sha256']
tree(regression,'tests/lookup-regression')
for platform in ['macos','device','simulator']:
 build_root=R/'generated/native-win32'/platform;build=json.loads((build_root/'result.json').read_text())
 assert build['bindings_sha256']==sha(R/'port/llasm-runtime/win32-bindings.json')
 assert build['objects']['guest_modules']['source_sha256']==sha(R/'port/core/guest_modules.c')
 for name in ['guest_modules','bindings']:
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
for p in ['port/llasm-runtime/win32-bindings.json','scripts/freeze-module-ex.py','scripts/build-native-win32.py','scripts/build-checked-runtime.py']:add(R/p,'sources/'+p)
add(R/'generated/integration-review/native-system-info-checkpoint-20261003/checkpoint.json','dependencies/preceding-import.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_exit=run['exit'],continuation_result=run['result'],scope='GetModuleHandleEx A/W uses audited mapped-image extents and atomic retained references, with matched FreeLibrary releases, unchanged references and permanent pinning. Actual ABI, 12000 concurrent cycles per host normal/TSan/guarded and iPad Simulator run, protected buffers and existing provider regressions. Three platform builds and actual original startup continuation. Last linked-image unload remains an explicit detach/unmap gate; no guest thread, visible original game, physical-device or authenticated-match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen',len(files),'files;',run['result'])
