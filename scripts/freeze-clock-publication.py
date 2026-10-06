#!/usr/bin/env python3
"""Preserve the accepted two-store clock revision and independent evidence."""
import datetime,json,pathlib,subprocess
from clock_patch_inputs import R,OUT,sha
out=R/'generated/integration-review/clock-publication-20261003';assert not (out/'checkpoint.json').exists();files=[]
def add(src,rel):
    dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    if not dst.exists():subprocess.run(['cp','-c',str(src),str(dst)],check=True)
    assert sha(src)==sha(dst),rel;files.append(dict(original=str(src.relative_to(R)),path=rel,sha256=sha(dst)))
def tree(src,rel):
    for p in sorted(src.rglob('*')):
        if p.is_file():add(p,str(pathlib.Path(rel)/p.relative_to(src)))
proof=json.loads((OUT/'native-comparison.json').read_text());assert proof['passed'] and not proof['cpu_differences'] and not proof['memory_differences']
native=(R/proof['run_path']).parent;assert sha(native/'result.json')==proof['run_sha256']
run=json.loads((native/'result.json').read_text());assert run['exit']==71 and run['result']['kind']=='clock_patch_native_return'
assert run['binary_sha256']==sha(native/'bootstrap') and run['external_build_sha256']==proof['guard_sha256']==sha(native/'guarded/result.json')
test=json.loads((OUT/'test-result.json').read_text());assert test['passed']
for p,h in test['sources_sha256'].items():assert sha(R/p)==h,p
for p,h in test['output_sha256'].items():assert sha(OUT/p)==h,p
prep=json.loads((OUT/'preparation.json').read_text());assert test['preparation_sha256']==proof['preparation_sha256']==sha(OUT/'preparation.json')
for p,h in prep['inputs_sha256'].items():assert sha(R/p)==h,p
assert proof['target_test_sha256']==sha(OUT/'test-result.json') and proof['script_sha256']==sha(R/'scripts/test-native-clock-patch.py')
assert proof['reference_helper_sha256']==sha(R/'scripts/clock_patch_inputs.py')
for name in ['code-revision','clock-patch-routes']:
    p=R/'generated/integration-review'/name;report=json.loads((p/'result.json').read_text());assert report['passed']
    if name=='code-revision':
        for n,h in report['sources_sha256'].items():assert sha(R/n)==h
    else:
        assert report['harness_sha256']==sha(R/'tests/qol_native_bootstrap.c') and report['engine_sha256']==sha(native/'engine-routed.ll')
        assert report['script_sha256']==sha(R/'scripts/test-clock-patch-routes.py')
    tree(p,'tests/'+name)
tree(OUT,'candidate')
for key,folder in [('clock_patch_checkpoint','before'),('clock_patch_final','after')]:
    for n,row in run[key].items():assert sha(R/row['path'])==row['sha256'];add(R/row['path'],folder+'/'+n)
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','clock-patch-inputs.json','clock_patch_publication.inc','support/result.json','guarded/result.json','guarded/policy.json']:add(native/n,'native/'+n)
for p in sorted(native.glob('*.ll')):add(p,'native/llvm/'+p.name)
for n in ['scripts/clock_patch_inputs.py','scripts/prepare-clock-patch.py','scripts/test-clock-patch.py','scripts/clock_patch_runtime_inputs.py','scripts/patch_runtime_inputs.py','scripts/test-qol-native-bootstrap.py','scripts/test-native-clock-patch.py','scripts/test-clock-patch-routes.py','scripts/test-code-revision.py','scripts/freeze-clock-publication.py','tests/qol_native_bootstrap.c','tests/code_revision_test.c','port/core/guest_memory_checks.c','port/core/guest_memory_checks.h']:add(R/n,'sources/'+n)
add(R/'generated/integration-review/clock-patch-checkpoint-20261003/checkpoint.json','dependencies/pre-write.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,native_revision=13,call_sites=[0x58a965,0x50cb12],targets=[0x1005f0d0,0x1005f0d0],guest_bytes_compared=proof['guest_bytes_compared'],
    scope='Both clock CALL redirects accepted through exact ordered publication and native/original-x86 suffix comparison. Seven caller/clock blocks and512 complete counter-to-milliseconds paths with original CRT arithmetic and independent integer formula checked. Earlier routes retained. Complete initializer, whole engine target paths, rendered behavior and authenticated play remain unverified.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; thirteenth finite revision')
