#!/usr/bin/env python3
"""Preserve native SDL integration checks and the actual next startup failure."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/sdl-backend-checkpoint-20261003'
assert not (out/'checkpoint.json').exists()
files=[];seen=set()
def add(p,rel):
    if rel in seen:return
    dest=out/rel;dest.parent.mkdir(parents=True,exist_ok=True)
    if not dest.exists():subprocess.run(['cp','-c',str(p),str(dest)],check=True)
    assert sha(p)==sha(dest),rel
    files.append(dict(original=str(p.relative_to(R)),path=rel,sha256=sha(dest)));seen.add(rel)
def tree(p,rel):
    for f in sorted(p.rglob('*')):
        if f.is_file():add(f,str(pathlib.Path(rel)/f.relative_to(p)))
tests=R/'generated/integration-review/sdl-hints';proof=json.loads((tests/'result.json').read_text());assert proof['passed']
for p,h in proof['sources_sha256'].items():assert sha(R/p)==h,p;add(R/p,'sources/'+p)
for mode in ['normal','tsan','checked']:assert sha(tests/('test-'+mode))==proof[mode]['binary_sha256']
assert sha(tests/'checked/result.json')==proof['checked_sha256']
assert sha(tests/'guarded/result.json')==proof['guarded_sha256']
tree(tests,'tests/hints')
mobile=json.loads((tests/'mobile-compilation.json').read_text());assert mobile['passed']
for platform in ['macos','device','simulator']:
    root=R/'generated/sdl-backend'/platform;build=json.loads((root/'result.json').read_text());assert build['passed']
    assert sha(R/'config/runtime/sdl-backend.json')==build['config_sha256']
    assert sha(R/'scripts/build-sdl-backend.py')==build['script_sha256']
    for p,h in build['libraries'].items():assert sha(R/p)==h,p;add(R/p,'backend/'+platform+'/'+pathlib.Path(p).name)
    for p,h in build['patches'].items():assert sha(root/'source'/p)==h,p;add(root/'source'/p,'backend/'+platform+'/source/'+p)
    for p,h in build['headers'].items():assert sha(root/'install'/p)==h,p;add(root/'install'/p,'backend/'+platform+'/'+p)
    for name in ['result.json','configure.log','build.log','install.log']:add(root/name,'backend/'+platform+'/'+name)
    if platform!='macos':
        row=mobile['platforms'][platform];assert sha(root/'guest_sdl_backend.o')==row['object_sha256']
        assert sha(root/'result.json')==row['backend_sha256'] and sha(R/'port/core/guest_sdl_backend.c')==row['source_sha256']
        add(root/'guest_sdl_backend.o','backend/'+platform+'/guest_sdl_backend.o')
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
run=json.loads((native/'result.json').read_text())
assert run['initialized'] and run['result']['engine_entered'] and not run['playable']
assert run['exit']==-6 and run['result']['kind']=='memory_violation' and '0x58a966' in run['result']['reason']
assert sha(native/'bootstrap')==run['binary_sha256'] and sha(native/'guarded/result.json')==run['external_build_sha256']
assert sha(R/'generated/sdl-backend/macos/result.json')==run['sdl_backend']['backend_report_sha256']==proof['backend_sha256']
for p,h in run['source_sha256'].items():assert sha(R/p)==h,p;add(R/p,'sources/'+p)
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json','link.log']:add(native/n,'native/'+n)
for p in sorted(native.glob('*.ll')):add(p,'native/llvm/'+p.name)
external=R/'generated/integration-review/external-memory-pass';er=json.loads((external/'result.json').read_text());assert er['pass_']
for p,h in er['plugins'].items():assert sha(R/p)==h,p
for p,h in er['sources_sha256'].items():assert sha(R/p)==h,p
tree(external,'tests/external-regression')
for n in ['scripts/freeze-sdl-backend.py','scripts/build-sdl-backend.py','scripts/compile-sdl-bridge.py','scripts/build-http-backend.py','scripts/checked_support.py','scripts/guard_checked_link.py','config/runtime/sdl-backend.json','dependencies.lock.json','port/licenses/SDL3-LICENSE.txt']:add(R/n,'sources/'+n)
for n in ['ExternalMemoryPass.dylib','result.json']:add(R/'generated/tools/external-memory-pass'/n,'compiler/'+n)
add(R/'generated/integration-review/sdl-hint-checkpoint-20261003/checkpoint.json','dependencies/preceding-hint.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_exit=run['exit'],continuation_result=run['result'],
    scope='Real native SDL hints and guest allocator/ABI/environment integration. All three static backend builds, mobile bridge compilation, host normal/TSan/both-guard tests and actual next protected-code-write failure. No thirteenth AOT revision, window, rendering, mobile execution or authenticated gameplay acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; SDL backend and actual continuation')
