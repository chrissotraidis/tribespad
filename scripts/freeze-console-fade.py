#!/usr/bin/env python3
"""Preserve the first console redirect proof before continuing native startup."""
import datetime, json, pathlib, subprocess
from console_fade_inputs import R, OUT, sha

out = R/'generated/integration-review/console-fade-checkpoint-20261003'
assert not (out/'checkpoint.json').exists()
files = []
def add(src,rel):
    dst=out/rel; dst.parent.mkdir(parents=True,exist_ok=True)
    if not dst.exists(): subprocess.run(['cp','-c',str(src),str(dst)],check=True)
    assert sha(src)==sha(dst),rel
    files.append(dict(original=str(src.relative_to(R)),path=rel,sha256=sha(dst)))
def tree(src,rel):
    for f in sorted(src.rglob('*')):
        if f.is_file(): add(f,str(pathlib.Path(rel)/f.relative_to(src)))
proof=json.loads((OUT/'native-comparison.json').read_text()); assert proof['passed'] and not proof['cpu_differences'] and not proof['memory_differences']
native=(R/proof['run_path']).parent
assert sha(native/'result.json')==proof['run_sha256']
run=json.loads((native/'result.json').read_text()); assert run['exit']==71 and run['result']['kind']=='console_fade_native_return'
assert run['binary_sha256']==sha(native/'bootstrap')
assert run['external_build_sha256']==proof['guard_sha256']==sha(native/'guarded/result.json')
test=json.loads((OUT/'test-result.json').read_text()); assert test['passed']
for path,h in test['sources_sha256'].items(): assert sha(R/path)==h,path
prep=json.loads((OUT/'preparation.json').read_text())
assert test['preparation_sha256']==proof['preparation_sha256']==sha(OUT/'preparation.json')
for path,h in prep['inputs_sha256'].items(): assert sha(R/path)==h,path
assert proof['target_test_sha256']==sha(OUT/'test-result.json')
assert proof['script_sha256']==sha(R/'scripts/test-native-console-fade.py')
assert test['binary_sha256']==sha(OUT/'test.dylib')
for name in ['code-revision','console-fade-routes']:
    path=R/'generated/integration-review'/name; report=json.loads((path/'result.json').read_text()); assert report['passed']
    if name=='code-revision':
        for n,h in report['sources_sha256'].items(): assert sha(R/n)==h
    else:
        assert report['harness_sha256']==sha(R/'tests/qol_native_bootstrap.c')
        assert report['engine_sha256']==sha(native/'engine-routed.ll')
        assert report['script_sha256']==sha(R/'scripts/test-console-fade-routes.py')
    tree(path,'tests/'+name)
tree(OUT,'candidate')
for key,folder in [('console_patch_checkpoint','before'),('console_fade_final','after')]:
    for name,row in run[key].items():
        assert sha(R/row['path'])==row['sha256']; add(R/row['path'],folder+'/'+name)
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','console-fade-inputs.json','console_fade_publication.inc','support/result.json','guarded/result.json','guarded/policy.json']:
    add(native/name,'native/'+name)
for f in sorted(native.glob('*.ll')): add(f,'native/llvm/'+f.name)
for name in ['scripts/console_fade_inputs.py','scripts/prepare-console-fade.py','scripts/test-console-fade.py','scripts/console_fade_runtime_inputs.py','scripts/patch_runtime_inputs.py','scripts/test-qol-native-bootstrap.py','scripts/test-native-console-fade.py','scripts/test-console-fade-routes.py','scripts/test-code-revision.py','scripts/freeze-console-fade.py','tests/qol_native_bootstrap.c','tests/code_revision_test.c','port/core/guest_memory_checks.c','port/core/guest_memory_checks.h']:
    add(R/name,'sources/'+name)
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,
    native_revision=8,call_site=0x5d285f,target=0x10002550,guest_bytes_compared=proof['guest_bytes_compared'],
    scope='First console CALL redirect accepted through exact single-store publication and native/original-x86 comparison. Twenty-block fade target validated on a finite exact-arithmetic corpus. All earlier routes retained. Later console patches, original UI/rendering and authenticated play remain unverified.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files: assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; eighth finite revision only')
