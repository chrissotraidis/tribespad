#!/usr/bin/env python3
"""Freeze tested byte atomic, actual original return, and unpublished next prefix."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/byte-cas-checkpoint-20261003';assert not (out/'checkpoint.json').exists();files={};sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def add(p):
 p=p.resolve();rel=p.relative_to(R)
 if str(rel) in files:return
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);subprocess.run(['cp','-c',str(p),str(dst)],check=True);assert sha(p)==sha(dst);files[str(rel)]=sha(dst)
def sources(d,key):
 for n,h in d[key].items():assert sha(R/n)==h,n;add(R/n)
def binary(p,h):assert sha(p)==h,p;add(p)
for name in ['locked-cmpxchg-byte','locked-cmpxchg','locked-cmpxchg8b']:
 root=R/'generated/integration-review'/name;d=json.loads((root/'result.json').read_text());assert d['passed'];add(root/'result.json');sources(d,'source_sha256')
 binary(root/'blocks.dylib',d['library_sha256'])
 for n,row in d['contention'].items():binary(root/n,row['binary_sha256'])
 for n in ['blocks.ll','blocks.llasm','helper.ll']:add(root/n)
root=R/'generated/integration-review/locked-cmpxchg-byte';d=json.loads((root/'dll-audit.json').read_text());assert d['passed'] and d['changed']==['loc_1007FF5C'];assert d['test_sha256']==sha(root/'result.json');sources(d,'sources_sha256');add(root/'dll-audit.json');add(root/'initial-fixture-boundary.log')
mobile=root/'B8138F85-0AE9-4494-9564-EDC7FD4E3B7F';d=json.loads((mobile/'result.json').read_text());assert d['passed'] and d['faults']==5;sources(d,'sources_sha256');binary(mobile/'test',d['binary_sha256']);binary(mobile/'device-atomic.o',d['device_object_sha256']);binary(mobile/'checked/result.json',d['checked_sha256']);add(mobile/'result.json')
for p in mobile.glob('*.log'):add(p)
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';d=json.loads((native/'result.json').read_text());assert d['initialized'] and d['exit']==-6 and d['result']['kind']=='memory_violation' and '0x5bb96b' in d['result']['reason'];sources(d,'source_sha256');binary(native/'bootstrap',d['binary_sha256']);binary(native/'guarded/result.json',d['external_build_sha256'])
for n in ['result.json','stdout.log','stderr.log','engine.bin','qol.bin','support/result.json','guarded/policy.json']:add(native/n)
g=json.loads((native/'guarded/result.json').read_text());assert g['plugin_build_sha256']==sha(R/'generated/tools/external-memory-pass/result.json') and g['config_sha256']==sha(R/'config/runtime/external-memory-contracts.json')
cap=root/'continuation';c=json.loads((cap/'worker-execution.json').read_text());assert c['verified_original_x86'] and not c['suspended'];assert c['binary_sha256']==d['binary_sha256'];binary(cap/'worker-lldb.log',c['log_sha256']);binary(cap/'capture_worker.py',c['capture_script_sha256']);binary(cap/'run_capture.py',c['driver_sha256']);binary(R/'scripts/verify-byte-cas-capture.py',c['verification_script_sha256'])
for p in cap.rglob('*'):
 if p.is_file() and p.suffix in ['.json','.log','.py']:add(p)
prefix=R/'generated/integration-review/post-video-prefix';p=json.loads((prefix/'preparation.json').read_text());t=json.loads((prefix/'test-result.json').read_text());assert p['passed'] and t['passed'] and not p['published'] and not t['published'] and t['cases']==512
assert t['preparation_sha256']==sha(prefix/'preparation.json');sources(p,'inputs_sha256');sources(t,'sources_sha256')
for report in [p,t]:
 for n,h in report['outputs_sha256'].items():binary(prefix/n,h)
add(prefix/'preparation.json');add(prefix/'test-result.json')
for n in ['scripts/freeze-byte-cas.py','config/runtime/locked-cmpxchg-byte.json','port/llasm-support/tribes_atomic.llinc','generated/tools/SRW-tribes/tribes-build.json','scripts/build-tribes-lifter.py','scripts/update-lifter-patch.py','generated/tools/external-memory-pass/result.json','generated/integration-review/curl-threads-checkpoint-20261003/checkpoint.json','docs/STATUS.md','docs/EXECUTION-PLAN.md']:add(R/n)
report=dict(passed=True,goal_complete=False,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_result=d['result'],scope='Byte atomic verified in components and actual original worker. Full launch remains a rejected main code store. Two candidate post-video blocks validated but unpublished; no14th revision or gameplay acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for n,h in files.items():assert sha(out/n)==h
print('PASS:',len(files),'source-verified files frozen; goal remains active')
