#!/usr/bin/env python3
"""Save the original state before the eighth candidate patch; admit no write."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/console-patch-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
files=[]
def add(src,rel):
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if not dst.exists():subprocess.run(['cp','-c',str(src),str(dst)],check=True)
 assert sha(src)==sha(dst),rel
 files.append(dict(original=str(src.relative_to(R)),path=rel,sha256=sha(dst)))
native=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
run=json.loads((native/'result.json').read_text());assert run['exit']==71 and run['result']['kind']=='console_patch_checkpoint' and run['result']['resume_pc']==0x1000292d
assert run['string_patch_publication'] and run['binary_sha256']==sha(native/'bootstrap')
for name,row in run['console_patch_checkpoint'].items():
 assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'capture/'+name)
analysis=R/'generated/integration-review/console-patch-analysis/result.json';data=json.loads(analysis.read_text());assert data['passed'] and data['before_target']==0x5de240 and data['after_target']==0x10002550
for n,h in data['inputs_sha256'].items():assert sha(R/n)==h,n
add(analysis,'analysis.json')
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name,'continuation/'+name)
for p in sorted(native.glob('*.ll')):add(p,'continuation/llvm/'+p.name)
for name in ['tests/qol_native_bootstrap.c','scripts/test-qol-native-bootstrap.py','scripts/analyze-console-patch.py','scripts/freeze-console-patch-capture.py','port/core/guest_memory_checks.c','port/core/guest_memory_checks.h','docs/STATUS.md']:add(R/name,'sources/'+name)
add(R/'generated/integration-review/console-patch-capture.log','capture.log')
add(R/'generated/integration-review/monitor-checkpoint-20261003/checkpoint.json','dependencies/monitor.json')
table=json.loads((native/'console-patch-pages.json').read_text())
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,readable_bytes=sum(r['size'] for r in table['ranges']),ranges=len(table['ranges']),resume_pc=0x1000292d,native_write_admitted=False,analysis=data['scope'],scope='Original native state before a newly reached console-initialization CALL displacement patch. Local four-instruction x86 reconstruction only; seven native phases remain unchanged. Complete routine/route/target validation and original/native differential comparison remain open. No gameplay or renderer acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; no new native code revision')
