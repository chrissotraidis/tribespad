#!/usr/bin/env python3
"""Preserve the condition-variable contract, native/guarded checks and actual continuation."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/condition-variables-checkpoint-20261003'
assert not (out/'checkpoint.json').exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
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
tests=R/'generated/integration-review/condition-variables'
proof=json.loads((tests/'result.json').read_text());assert proof['passed']
for name,h in proof['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name,'sources/'+name)
for mode in ['normal','tsan','checked']:
    row=proof[mode];assert row['passed']
    assert sha(tests/('test-'+mode))==row['binary_sha256']
assert sha(tests/'guarded/result.json')==proof['guarded_sha256']
assert sha(tests/'checked/result.json')==proof['checked_sha256']
mobile=json.loads((tests/'simulator/result.json').read_text());assert mobile['passed']
assert mobile['host_proof_sha256']==sha(tests/'result.json')
assert mobile['binary_sha256']==sha(tests/'simulator/condition-test')
for name,h in mobile['sources_sha256'].items():assert sha(R/name)==h,name
assert mobile['script_sha256']==sha(R/'scripts/test-condition-simulator.py')
add(R/'scripts/test-condition-simulator.py','sources/scripts/test-condition-simulator.py')
tree(tests,'tests/condition')
add(R/'tests/critical_section_test.c','sources/tests/critical_section_test.c')
external=R/'generated/integration-review/external-memory-pass';er=json.loads((external/'result.json').read_text());assert er['pass_']
for p,h in er['plugins'].items():assert sha(R/p)==h,p
for p,h in er['sources_sha256'].items():assert sha(R/p)==h,p
tree(external,'tests/external-regression')
for platform in ['macos','device','simulator']:
    root=R/'generated/native-win32'/platform;build=json.loads((root/'result.json').read_text())
    assert build['bindings_sha256']==sha(R/'port/llasm-runtime/win32-bindings.json')
    assert build['objects']['guest_condition']['source_sha256']==sha(R/'port/core/guest_condition.c')
    for n in ['guest_condition','critical_section','bindings']:
        assert sha(root/(n+'.o'))==build['objects'][n]['object_sha256'];add(root/(n+'.o'),'platforms/'+platform+'/'+n+'.o')
    add(root/'result.json','platforms/'+platform+'/result.json')
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
run=json.loads((native/'result.json').read_text());assert run['state']=='finished' and run['initialized'] and run['clock_patch_publication']
assert run['result']['engine_entered'] and not run['playable']
assert sha(native/'bootstrap')==run['binary_sha256'] and sha(native/'guarded/result.json')==run['external_build_sha256']
normal=R/'generated/native-win32/macos/result.json';checked=R/'generated/native-win32/checked-macos/result.json'
assert sha(normal)==run['runtime_build_sha256'] and sha(checked)==run['checked_runtime_build_sha256']
add(checked,'runtime/checked.json')
for p,h in run['source_sha256'].items():assert sha(R/p)==h,p;add(R/p,'sources/'+p)
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(native/n,'continuation/'+n)
for p in sorted(native.glob('*.ll')):add(p,'continuation/llvm/'+p.name)
for key,items in run.items():
    if not key.endswith(('_checkpoint','_final')) or not isinstance(items,dict):continue
    for name,row in items.items():
        if isinstance(row,dict) and 'path' in row and 'sha256' in row:
            assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'continuation/capture/'+name)
for n in ['ExternalMemoryPass.dylib','result.json']:add(R/'generated/tools/external-memory-pass'/n,'compiler/'+n)
for n in ['scripts/freeze-condition.py','scripts/build-native-win32.py','scripts/build-external-memory-pass.py','scripts/guard_checked_link.py']:add(R/n,'sources/'+n)
add(R/'generated/integration-review/clock-continuation-20261003/checkpoint.json','dependencies/preceding-import.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_exit=run['exit'],continuation_result=run['result'],
    scope='Five native condition-variable services with atomic unlock/wait, original-mode reacquisition, timeout and matched CS lifetime. Actual ARM64 wrappers, normal/TSan/guarded concurrency tests, compiler/lifetime rejection tests and prior critical-section regression. Apple platform compilation and original startup continuation. Single iPad Simulator service execution. No guest thread creation, cancellation, fairness, physical device, original menu or authenticated gameplay acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; condition-variable contract and native continuation')
