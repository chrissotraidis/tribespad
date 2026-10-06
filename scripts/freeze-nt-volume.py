#!/usr/bin/env python3
"""Freeze source-verified NT volume queries and actual original-client continuation."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/nt-volume-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();files={}
def add(p):
 p=p.resolve();rel=p.relative_to(R);dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if str(rel) in files:return
 subprocess.run(['cp','-c',str(p),str(dst)],check=True);assert sha(p)==sha(dst);files[str(rel)]=sha(dst)
for rel in ['nt-volume/macos','nt-volume/B8138F85-0AE9-4494-9564-EDC7FD4E3B7F','file-security']:
 root=R/'generated/integration-review'/rel;report=json.loads((root/'result.json').read_text());assert report['passed'];add(root/'result.json')
 for name,h in report['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name)
 for row in report['runs']:
  binary=root/(row['mode'] if rel=='file-security' else 'test-'+row['mode']);assert sha(binary)==row['binary_sha256'];add(binary)
 for p in root.iterdir():
  if p.is_file() and p.suffix=='.log':add(p)
root=R/'generated/native-win32/current-directory-regressions';r=json.loads((root/'result.json').read_text());assert r['pass'];add(root/'result.json');assert sha(R/'scripts/test-filesystem-regressions.py')==r['test_driver_sha256'];add(R/'scripts/test-filesystem-regressions.py')
for row in r['tests'].values():
 for name,h in row['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name)
for platform in ['macos','device','simulator']:
 p=R/'generated/native-win32'/platform/'result.json';r=json.loads(p.read_text());add(p)
 for name in ['guest_file','guest_filesystem','native_handles']:assert r['objects'][name]['source_sha256']==sha(R/'port/core'/(name+'.c'))
p=R/'generated/native-win32/checked-macos/result.json';add(p)
root=R/'generated/integration-review/system-providers';r=json.loads((root/'result.json').read_text());assert r['pass'] and r['runtime_build_sha256']==sha(R/'generated/native-win32/macos/result.json');add(root/'result.json');assert sha(root/'native')==r['binary_sha256'];add(root/'native');add(root/'run.log')
for name,h in r['source_sha256'].items():assert sha(R/name)==h,name;add(R/name)
for folder in ['current-directory-abi','file-api-encoding-abi']:
 root=R/'generated/native-win32'/folder/'macos';r=json.loads((root/'result.json').read_text());assert r['pass'];add(root/'result.json')
 for name,h in r['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name)
 for row in r['runs']:
  p=root/row['mode'];assert sha(p)==row['binary_sha256'];add(p)
root=R/'generated/integration-review/external-memory-pass';r=json.loads((root/'result.json').read_text());assert r['pass_'];add(root/'result.json');assert sha(root/'test')==r['binary_sha256'];add(root/'test');add(root/'run.log')
for name,h in r['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name)
for name,h in r['plugins'].items():assert sha(R/name)==h,name;add(R/name)
native=R/'generated/srw/run-20261003T150627-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';r=json.loads((native/'result.json').read_text());assert r['state']=='finished' and r['initialized'];assert sha(native/'bootstrap')==r['binary_sha256']
for name,h in r['source_sha256'].items():assert sha(R/name)==h,name;add(R/name)
for name in ['bootstrap','engine.bin','qol.bin','result.json','stdout.log','stderr.log','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name)
for name in ['port/core/guest_file.h','port/core/native_handles.h','port/core/guest_memory_checks.h','port/compiler/ExternalMemoryPass.cpp','config/runtime/nt-volume.json','scripts/freeze-nt-volume.py','scripts/test-dinput-simulator.py','scripts/test-engine-bootstrap.py','scripts/test-current-directory-abi.py','scripts/test-file-api-encoding-abi.py','generated/tools/external-memory-pass/result.json','generated/integration-review/file-attributes-checkpoint-20261003/checkpoint.json','docs/STATUS.md','docs/EXECUTION-PLAN.md']:add(R/name)
evidence=R/'generated/integration-review/nt-volume'
for p in sorted(evidence.iterdir()):
 if p.is_file() and p.suffix in ['.json','.log','.py']:add(p)
(out/'checkpoint.json').write_text(json.dumps(dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_exit=r['exit'],continuation_result=r['result'],scope='Real NT device-information query passes Mac normal/TSan/guarded ABI and sole guarded iPad, with actual dynamic-provider invocation and existing file consumers. Actual original-client continuation is retained separately. No original window, map, controls, HD or authenticated match acceptance.'),indent=2)+'\n')
for name,h in files.items():assert sha(out/name)==h
print('PASS:',len(files),'source-verified files frozen')
