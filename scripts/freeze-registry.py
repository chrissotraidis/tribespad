#!/usr/bin/env python3
"""Save the checked hardware registry evidence and the resulting startup boundary."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/registry-checkpoint-20261003'
assert not (out/'checkpoint.json').exists()
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
def tree(src,rel):
 for p in sorted(src.rglob('*')):
  if p.is_file():add(p,str(pathlib.Path(rel)/p.relative_to(src)))
tests=R/'generated/integration-review/registry'
for platform in ['macos','simulator']:
 p=tests/platform;d=json.loads((p/'result.json').read_text());assert d['passed']
 assert all(sha(R/n)==h for n,h in d['sources_sha256'].items())
 assert sha(p/'checked/result.json')==d['checked_sha256'] and sha(p/'guarded/result.json')==d['guarded_sha256']
 assert sha(p/'test-checked')==d['checked']['binary_sha256']
 if platform=='macos':assert d['normal']['passed'] and d['tsan']['passed']
tree(tests,'tests')
crash=R/'generated/integration-review/registry-crash';analysis=json.loads((crash/'analysis.json').read_text());assert analysis['passed'] and all(sha(R/n)==h for n,h in analysis['sources_sha256'].items());tree(crash,'crash')
tree(R/'ref/downloads/libuv-cpu-reference','libuv-reference')
provider=R/'generated/integration-review/system-providers';pd=json.loads((provider/'result.json').read_text());assert pd['pass'] and pd['runtime_build_sha256']==sha(R/'generated/native-win32/macos/result.json')
tree(provider,'provider-tests')
for platform in ['macos','device','simulator']:
 b=R/'generated/native-win32'/platform;d=json.loads((b/'result.json').read_text())
 assert d['objects']['guest_registry']['source_sha256']==sha(R/'port/core/guest_registry.c')
 for name in ['result.json','guest_registry.o','native_system_providers.o','bindings.ll','bindings.o']:add(b/name,'builds/'+platform+'/'+name)
b=R/'generated/native-win32/checked-macos';d=json.loads((b/'result.json').read_text());assert d['state']=='finished' and not d['failures']
for name in ['result.json','guest_registry.ll','guest_registry.bc','guest_registry.o']:add(b/name,'builds/checked/'+name)
native=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text())
assert run['state']=='finished' and run['exit']==-10 and run['string_patch_publication']
# SIGBUS produced no final JSON stop object; keep the driver's false summary
# flags intact and validate reached execution using its actual stderr evidence.
assert 'Returned original DLL stage 1 EAX=1' in run['stderr']
assert 'Entering original patched engine at 0x006fde60' in run['stderr']
assert run['binary_sha256']==sha(native/'bootstrap') and run['external_build_sha256']==sha(native/'guarded/result.json')
assert len(run['ntdll_probe']['probes'])==6
assert run['registry_events'] and run['registry_events'][0]['operation']==0 and run['registry_events'][0]['result']==0
assert any(e['operation']==1 and e['auxiliary']==0x1012a910 and e['result']==2 for e in run['registry_events'])
assert all(p['name']!='NtQuerySystemInformation' for p in run['ntdll_probe']['probes'])
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name,'continuation/'+name)
for p in sorted(native.glob('*.ll')):add(p,'continuation/llvm/'+p.name)
source_names=['port/core/guest_registry.c','port/core/guest_registry.h','port/core/native_system_providers.inc','port/compiler/ExternalMemoryPass.cpp','port/llasm-runtime/win32-bindings.json','config/runtime/native-providers.json','config/runtime/external-memory-contracts.json','scripts/build-native-win32.py','scripts/guard_checked_link.py','scripts/test-registry.py','scripts/test-qol-native-bootstrap.py','scripts/ntdll_probe_inputs.py','scripts/freeze-registry.py','scripts/analyze-registry-crash.py','tests/registry_test.c','tests/fixtures/registry_dispatch.c','tests/fixtures/native_enter.ll','tests/qol_native_bootstrap.c','docs/STATUS.md']
for name in source_names:add(R/name,'sources/'+name)
for name in ['registry-tests.log','registry-ipad.log','registry-native-build.log','registry-device-build.log','registry-simulator-build.log','registry-checked-build.log','registry-external-regression.log','registry-providers-test.log','registry-native-continuation.log']:add(R/'generated/integration-review'/name,'logs/'+name)
add(R/'generated/tools/external-memory-pass/result.json','builds/external-pass.json')
add(R/'generated/integration-review/system-query-checkpoint-20261003/checkpoint.json','dependencies/system-query.json')
add(R/'generated/integration-review/registry-superseded-build.txt','logs/superseded-build.txt')
add(R/'generated/integration-review/registry-host-reference.json','host-reference.json')
report=dict(created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=True,files=files,continuation=run['result'],exit=run['exit'],registry_events=run['registry_events'],crash_analysis_sha256=sha(crash/'analysis.json'),remaining_nt_probes=6,scope='Read-only native CPU hardware registry through checked ABI on macOS and iPad Simulator. Unknown host frequency stays absent; actual original registry call results recorded. Device compilation only; no menu, HD rendering or authenticated match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files')
