#!/usr/bin/env python3
"""Preserve native startup after animation publication, at the CMP translation gap."""
import datetime,json,pathlib,subprocess
from console_animation_inputs import R,sha
out=R/'generated/integration-review/highbyte-compare-checkpoint-20261003';assert not (out/'checkpoint.json').exists();files=[]
def add(src,rel):
    dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    if src!=dst and not dst.exists():subprocess.run(['cp','-c',str(src),str(dst)],check=True)
    assert sha(src)==sha(dst),rel;files.append(dict(original=str(src.relative_to(R)),path=rel,sha256=sha(dst)))
previous=json.loads((out/'preceding-continuation/checkpoint.json').read_text());assert previous['preserved_stop']
for row in previous['files']:assert sha(out/'preceding-continuation'/row['path'])==row['sha256']
for p in sorted((out/'preceding-continuation').rglob('*')):
    if p.is_file():add(p,str(p.relative_to(out)))
native=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text())
assert run['exit']==71 and run['result']['kind']=='instruction' and run['result']['index']==0x10115878 and run['console_animation_publication']
assert run['binary_sha256']==sha(native/'bootstrap')
for n,row in run['instruction_checkpoint'].items():assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'capture/'+n)
analysis=R/'generated/integration-review/highbyte-compare';data=json.loads((analysis/'analysis.json').read_text());assert data['passed']
for p,h in data['inputs_sha256'].items():assert sha(R/p)==h
for p in sorted(analysis.glob('*')):
    if p.is_file():add(p,'analysis/'+p.name)
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(native/n,'native/'+n)
for p in sorted(native.glob('*.ll')):add(p,'native/llvm/'+p.name)
for n in ['tests/qol_native_bootstrap.c','scripts/test-qol-native-bootstrap.py','scripts/analyze-highbyte-compare.py','scripts/freeze-highbyte-compare-capture.py','scripts/console_animation_inputs.py','scripts/patches/srw-tribes.patch','toolchains.lock.json','docs/archive/STATUS.md','docs/archive/EXECUTION-PLAN.md']:add(R/n,'sources/'+n)
add(R/'generated/tools/SRW-tribes/SR_full_llasm_instr.c','compiler/SR_full_llasm_instr.c')
add(R/'generated/integration-review/highbyte-compare-capture.log','capture.log')
add(R/'generated/integration-review/console-animation-publication-20261003/checkpoint.json','dependencies/console-animation.json')
table=json.loads((native/'instruction-pages.json').read_text())
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,pc=0x10115878,ranges=len(table['ranges']),readable_bytes=sum(r['size'] for r in table['ranges']),
    scope='Native startup after nine accepted revisions, with both next-hook protection requests observed, stopped at untranslated CMP memory/CH. Full CPU/readable guest state and original one-instruction replay preserved. Pending bounds-check hooks have not been accepted as published AOT routes; compiler repair and further startup remain open.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files at CMP translation gap')
