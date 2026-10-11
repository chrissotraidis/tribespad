#!/usr/bin/env python3
"""Preserve the next console patch state and the preceding protected-write stop."""
import datetime, json, pathlib, subprocess
from console_fade_inputs import R, sha

out=R/'generated/integration-review/console-animation-checkpoint-20261003'
assert not (out/'checkpoint.json').exists()
files=[]
def add(src,rel):
    dst=out/rel; dst.parent.mkdir(parents=True,exist_ok=True)
    if src!=dst and not dst.exists(): subprocess.run(['cp','-c',str(src),str(dst)],check=True)
    assert sha(src)==sha(dst),rel
    files.append(dict(original=str(src.relative_to(R)),path=rel,sha256=sha(dst)))
previous=json.loads((out/'preceding-continuation/checkpoint.json').read_text()); assert previous['preserved_failure']
for row in previous['files']: assert sha(out/'preceding-continuation'/row['path'])==row['sha256']
for p in sorted((out/'preceding-continuation').rglob('*')):
    if p.is_file(): add(p,str(p.relative_to(out)))
native=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
run=json.loads((native/'result.json').read_text()); assert run['exit']==71 and run['result']['kind']=='console_animation_checkpoint'
assert run['console_fade_publication'] and run['binary_sha256']==sha(native/'bootstrap')
for name,row in run['console_animation_checkpoint'].items():
    assert sha(R/row['path'])==row['sha256']; add(R/row['path'],'capture/'+name)
analysis=R/'generated/integration-review/console-animation-analysis/result.json'; data=json.loads(analysis.read_text()); assert data['passed']
for path,h in data['inputs_sha256'].items(): assert sha(R/path)==h,path
add(analysis,'analysis.json')
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:
    add(native/name,'native/'+name)
for p in sorted(native.glob('*.ll')): add(p,'native/llvm/'+p.name)
for name in ['tests/qol_native_bootstrap.c','scripts/test-qol-native-bootstrap.py','scripts/analyze-console-animation.py','scripts/freeze-console-animation-capture.py','scripts/console_fade_inputs.py','port/core/guest_memory_checks.c','port/core/guest_memory_checks.h','docs/archive/STATUS.md','docs/archive/EXECUTION-PLAN.md']:
    add(R/name,'sources/'+name)
add(R/'generated/integration-review/console-animation-capture.log','capture.log')
add(R/'generated/integration-review/console-fade-checkpoint-20261003/checkpoint.json','dependencies/console-fade.json')
table=json.loads((native/'console-animation-pages.json').read_text())
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,
    resume_pc=0x10002bc0,readable_bytes=sum(r['size'] for r in table['ranges']),ranges=len(table['ranges']),ninth_revision_admitted=False,
    scope='Original native state before the second console CALL displacement patch, after the accepted eighth revision. Preceding failed continuation preserved. Four-instruction x86 reconstruction only; target/body/publication validation remains open. No original menu or gameplay acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files: assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; next console patch remains blocked')
