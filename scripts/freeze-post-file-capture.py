#!/usr/bin/env python3
"""Freeze native state before the unaccepted tenth code revision."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/post-file-patch-checkpoint-20261003'
assert not (out/'checkpoint.json').exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=[]
def add(src,rel):
    dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    if not dst.exists():subprocess.run(['cp','-c',str(src),str(dst)],check=True)
    assert sha(src)==sha(dst),rel
    files.append(dict(original=str(src.relative_to(R)),path=rel,sha256=sha(dst)))
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
run=json.loads((native/'result.json').read_text())
assert run['exit']==71 and run['result']['kind']=='post_file_patch_checkpoint' and run['result']['resume_pc']==0x10015d0a
assert run['initialized'] and run['engine_entered'] and run['console_animation_publication']
assert sha(native/'bootstrap')==run['binary_sha256'] and sha(native/'guarded/result.json')==run['external_build_sha256']
for name,row in run['post_file_patch_checkpoint'].items():
    assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'capture/'+name)
analysis=R/'generated/integration-review/post-file-patch';a=json.loads((analysis/'analysis.json').read_text());assert a['passed']
for p,h in a['inputs_sha256'].items():assert sha(R/p)==h,p
assert sha(analysis/'reference-cpu.bin')==a['reference_cpu_sha256']
for p in sorted(analysis.glob('*')):
    if p.is_file():add(p,'analysis/'+p.name)
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:
    add(native/name,'native/'+name)
for p in sorted(native.glob('*.ll')):add(p,'native/llvm/'+p.name)
for name in ['tests/qol_native_bootstrap.c','scripts/test-qol-native-bootstrap.py','scripts/analyze-post-file-patch.py','scripts/freeze-post-file-capture.py']:
    add(R/name,'sources/'+name)
add(R/'generated/integration-review/createfile-frontier/post-file-capture.log','capture.log')
add(R/'generated/integration-review/file-security-checkpoint-20261003/checkpoint.json','dependencies/file-security.json')
table=json.loads((native/'post-file-patch-pages.json').read_text())
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,
            pc=0x10015d0a,writer=0x10015d13,call_site=0x5bff78,target=0x10015a10,
            ranges=len(table['ranges']),readable_bytes=sum(r['size'] for r in table['ranges']),
            scope='Actual native state after nine accepted revisions and existing-file open handling, immediately before the next protected CALL displacement write. Four original instructions replay successfully. No tenth revision or target-path behavioral acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; next unaccepted CALL revision')
