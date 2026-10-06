#!/usr/bin/env python3
"""Preserve the first real startup boundary beyond the allocation revision."""
import datetime,json,pathlib,subprocess
from allocation_patch_inputs import R,OUT,sha
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
out=R/'generated/integration-review/allocation-continuation-20261003'
assert not (out/'checkpoint.json').exists()
run=json.loads((native/'result.json').read_text())
assert run['initialized'] and run['allocation_patch_publication']
assert run['result']['engine_entered'] and run['result']['kind']!='allocation_patch_native_return'
assert run['binary_sha256']==sha(native/'bootstrap')
assert run['external_build_sha256']==sha(native/'guarded/result.json')
inputs=json.loads((native/'patch-inputs.json').read_text())
for p,h in inputs['sources_sha256'].items():assert sha(R/p)==h,p
assert inputs['allocation_patch']['preparation_sha256']==sha(OUT/'preparation.json')
assert inputs['allocation_patch']['test_sha256']==sha(OUT/'test-result.json')
files=[]
def add(p,rel):
    dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    if not dst.exists():subprocess.run(['cp','-c',str(p),str(dst)],check=True)
    assert sha(p)==sha(dst),rel
    files.append(dict(original=str(p.relative_to(R)),path=rel,sha256=sha(dst)))
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','allocation-patch-inputs.json','allocation_patch_publication.inc','guarded/result.json','guarded/policy.json','support/result.json']:
    add(native/name,'native/'+name)
for p in sorted(native.glob('*.ll')):add(p,'native/llvm/'+p.name)
for name in ['preparation.json','test-result.json','engine.ll','reference.json','isolated.ll','test-bridge.ll','test.dylib','next-startup.log','test-final.log']:
    add(OUT/name,'candidate/'+name)
for p in ['scripts/prepare-allocation-patch.py','scripts/test-allocation-patch.py','scripts/allocation_patch_inputs.py','scripts/allocation_patch_runtime_inputs.py','scripts/patch_runtime_inputs.py','scripts/test-qol-native-bootstrap.py','scripts/freeze-allocation-continuation.py','tests/qol_native_bootstrap.c','port/core/guest_memory_checks.c','port/core/guest_memory_checks.h']:
    add(R/p,'sources/'+p)
for key,items in run.items():
    if not key.endswith(('_checkpoint','_final')) or not isinstance(items,dict):continue
    for name,row in items.items():
        if isinstance(row,dict) and 'path' in row and 'sha256' in row:
            assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'capture/'+name)
add(R/'generated/integration-review/allocation-publication-20261003/checkpoint.json','dependencies/publication.json')
report=dict(preserved_stop=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),exit=run['exit'],result=run['result'],files=files,
    scope='Original native startup passes the accepted twelfth revision and reaches this actual next boundary. Preserved failure or diagnostic stop; no menu, rendering, gameplay or authenticated-match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('Preserved',len(files),'files;',report['result'])
