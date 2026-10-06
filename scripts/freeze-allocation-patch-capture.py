#!/usr/bin/env python3
"""Preserve the allocation-size update after the eleventh native revision."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/allocation-patch-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();files=[]
def add(src,rel):
    dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    if src!=dst and not dst.exists():subprocess.run(['cp','-c',str(src),str(dst)],check=True)
    assert sha(src)==sha(dst),rel;files.append(dict(original=str(src.relative_to(R)),path=rel,sha256=sha(dst)))
prior_path=R/'generated/integration-review/collision-continuation-20261003/checkpoint.json'
prior=json.loads(prior_path.read_text());assert prior['preserved_stop']
for row in prior['files']:assert sha(prior_path.parent/row['path'])==row['sha256']
add(prior_path,'dependencies/preceding-failure.json')
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text())
assert run['exit']==71 and run['result']['kind']=='protection_checkpoint' and run['result']['address']==0x615f14
assert run['collision_patch_publication'] and sha(native/'bootstrap')==run['binary_sha256']
for name,row in run['protection_checkpoint'].items():assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'capture/'+name)
analysis=R/'generated/integration-review/allocation-patch-analysis';a=json.loads((analysis/'analysis.json').read_text());assert a['passed']
for p,h in a['inputs_sha256'].items():assert sha(R/p)==h,p
for p in sorted(analysis.glob('*')):
    if p.is_file():add(p,'analysis/'+p.name)
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name,'native/'+name)
for p in sorted(native.glob('*.ll')):add(p,'native/llvm/'+p.name)
for name in ['tests/qol_native_bootstrap.c','scripts/test-qol-native-bootstrap.py','scripts/analyze-allocation-patch.py','scripts/freeze-allocation-patch-capture.py']:add(R/name,'sources/'+name)
add(R/'generated/integration-review/collision-patch-candidate/next-capture.log','capture.log');add(R/'generated/integration-review/collision-publication-20261003/checkpoint.json','dependencies/eleventh-publication.json')
table=json.loads((native/'protection-pages.json').read_text())
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,pc=a['pc'],writer=a['writer'],instruction=a['instruction'],old_size=a['old_size'],new_size=a['new_size'],ranges=len(table['ranges']),readable_bytes=sum(r['size'] for r in table['ranges']),
            scope='Native successful protection call after eleven accepted revisions, stopped before the twelfth code mutation; raw rejected-write run and original three-instruction replay retained. Allocating-block validation and publication remain unimplemented.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; next unaccepted allocation-size update')
