#!/usr/bin/env python3
"""Preserve existing-file attributes, host path checks and native continuation."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/file-security-checkpoint-20261003'
assert not (out/'checkpoint.json').exists()
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
files=[];seen=set()
def add(src,rel):
    if rel in seen:return
    dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    if not dst.exists():subprocess.run(['cp','-c',str(src),str(dst)],check=True)
    assert sha(src)==sha(dst),rel
    seen.add(rel);files.append(dict(original=str(src.relative_to(R)),path=rel,sha256=sha(dst)))
def tree(src,rel):
    for p in sorted(src.rglob('*')):
        if p.is_file():add(p,str(pathlib.Path(rel)/p.relative_to(src)))
tests=R/'generated/integration-review/file-security';proof=json.loads((tests/'result.json').read_text())
assert proof['passed'] and [r['mode'] for r in proof['runs']]==['normal','tsan','checked']
for name,h in proof['sources_sha256'].items():assert sha(R/name)==h;add(R/name,'sources/'+name)
for row in proof['runs']:
    assert sha(tests/row['mode'])==row['binary_sha256']
    assert sha(tests/(row['mode']+'.log'))==row['log_sha256']
assert sha(tests/'guarded/result.json')==proof['guarded_build_sha256']
assert sha(tests/'checked-support/result.json')==proof['checked_build_sha256']
tree(tests,'tests/security')
for name in ['file-security-regression','file-security-platforms']:
    root=R/'generated/integration-review'/name;report=json.loads((root/'result.json').read_text());assert report['passed']
    if 'sources_sha256' in report:
        for p,h in report['sources_sha256'].items():assert sha(R/p)==h,p
    else:assert sha(R/'port/core/guest_file.c')==report['source_sha256']
    tree(root,'tests/'+name)
external=R/'generated/integration-review/external-memory-pass';er=json.loads((external/'result.json').read_text());assert er['pass_']
for p,h in er['plugins'].items():assert sha(R/p)==h,p
tree(external,'tests/external-regression')
frontier=R/'generated/integration-review/createfile-frontier';fr=json.loads((frontier/'analysis.json').read_text());assert fr['passed']
for p,h in fr['inputs'].items():assert sha(frontier/p)==h,p
assert fr['script_sha256']==sha(R/'scripts/analyze-createfile-frontier.py')
tree(frontier,'frontier')
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
run=json.loads((native/'result.json').read_text());assert run['state']=='finished' and run['console_animation_publication']
assert not run['playable'] and 'Entering original patched engine at 0x006fde60\n' in run['stderr']
assert sha(native/'bootstrap')==run['binary_sha256'] and sha(native/'guarded/result.json')==run['external_build_sha256']
normal=R/'generated/native-win32/macos/result.json';checked=R/'generated/native-win32/checked-macos/result.json'
assert sha(normal)==run['runtime_build_sha256'] and sha(checked)==run['checked_runtime_build_sha256']
assert json.loads(normal.read_text())['objects']['guest_file']['source_sha256']==sha(R/'port/core/guest_file.c')
add(normal,'runtime/normal.json');add(checked,'runtime/checked.json')
for p,h in run['source_sha256'].items():assert sha(R/p)==h,p;add(R/p,'sources/'+p)
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:
    add(native/name,'continuation/'+name)
for p in sorted(native.glob('*.ll')):add(p,'continuation/llvm/'+p.name)
for name,row in run.get('instruction_checkpoint',{}).items():
    assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'continuation/capture/'+name)
for name in ['ExternalMemoryPass.dylib','result.json']:
    add(R/'generated/tools/external-memory-pass'/name,'compiler/'+name)
for name in ['scripts/freeze-file-security.py','scripts/analyze-createfile-frontier.py','scripts/build-external-memory-pass.py','scripts/guard_checked_link.py','port/core/guest_filesystem.h','port/core/native_handles.h']:
    add(R/name,'sources/'+name)
dependency=R/'generated/integration-review/highbyte-compare-repair-checkpoint-20261003/checkpoint.json'
assert sha(dependency)==fr['checkpoint_sha256'];add(dependency,'dependencies/highbyte-repair.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,
            continuation_exit=run['exit'],continuation_result=run['result'],
            scope='Existing-file SECURITY_ATTRIBUTES with real descriptor inheritance, exact guest ABI and guarded host-only path resolution. Component normal/ThreadSanitizer/checked execution, native exec inheritance, regression tests and changed-unit Apple compilation. Native startup result retained separately; no original menu, gameplay, mobile execution or authenticated-match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; existing-file attributes and native continuation')
