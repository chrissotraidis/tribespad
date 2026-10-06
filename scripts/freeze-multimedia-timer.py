#!/usr/bin/env python3
"""Preserve the timer contract, native/guarded checks and actual continuation."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/multimedia-timer-checkpoint-20261003'
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
tests=R/'generated/integration-review/multimedia-timer'
proof=json.loads((tests/'result.json').read_text());assert proof['passed']
for name,h in proof['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name,'sources/'+name)
for mode in ['normal','tsan','checked','injected']:
    row=proof[mode];assert row['passed']
    assert sha(tests/('test-errors' if mode=='injected' else 'test-'+mode))==row['binary_sha256']
assert sha(tests/'guarded/result.json')==proof['guarded_sha256']
assert sha(tests/'checked/result.json')==proof['checked_sha256']
tree(tests,'tests/timer')
external=R/'generated/integration-review/external-memory-pass';er=json.loads((external/'result.json').read_text());assert er['pass_']
for p,h in er['plugins'].items():assert sha(R/p)==h,p
for p,h in er['sources_sha256'].items():assert sha(R/p)==h,p
tree(external,'tests/external-regression')
for platform in ['macos','device','simulator']:
    root=R/'generated/native-win32'/platform;build=json.loads((root/'result.json').read_text())
    assert build['bindings_sha256']==sha(R/'port/llasm-runtime/win32-bindings.json')
    assert build['objects']['multimedia_timer']['source_sha256']==sha(R/'port/core/multimedia_timer.c')
    for n in ['multimedia_timer','bindings']:
        assert sha(root/(n+'.o'))==build['objects'][n]['object_sha256'];add(root/(n+'.o'),'platforms/'+platform+'/'+n+'.o')
    add(root/'result.json','platforms/'+platform+'/result.json')
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
run=json.loads((native/'result.json').read_text());assert run['state']=='finished' and run['initialized'] and run['allocation_patch_publication']
assert run['result']['engine_entered'] and not run['playable']
assert sha(native/'bootstrap')==run['binary_sha256'] and sha(native/'guarded/result.json')==run['external_build_sha256']
normal=R/'generated/native-win32/macos/result.json';checked=R/'generated/native-win32/checked-macos/result.json'
assert sha(normal)==run['runtime_build_sha256'] and sha(checked)==run['checked_runtime_build_sha256']
add(checked,'runtime/checked.json')
for p,h in run['source_sha256'].items():assert sha(R/p)==h,p;add(R/p,'sources/'+p)
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(native/n,'continuation/'+n)
for p in sorted(native.glob('*.ll')):add(p,'continuation/llvm/'+p.name)
for name,row in run.get('instruction_checkpoint',{}).items():
    assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'continuation/capture/'+name)
for n in ['ExternalMemoryPass.dylib','result.json']:add(R/'generated/tools/external-memory-pass'/n,'compiler/'+n)
for n in ['scripts/freeze-multimedia-timer.py','scripts/build-native-win32.py','scripts/build-external-memory-pass.py','scripts/guard_checked_link.py']:add(R/n,'sources/'+n)
add(R/'generated/integration-review/allocation-continuation-20261003/checkpoint.json','dependencies/preceding-import.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_exit=run['exit'],continuation_result=run['result'],
    scope='Native multimedia timer capability and matching period profile, boot-domain clock and ABI/guard/concurrency tests. Apple platform compilation, external compiler regression and real startup continuation. No callback timers, wake-up deadline guarantee, mobile execution, original menu or authenticated gameplay acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; timer contract and native continuation')
