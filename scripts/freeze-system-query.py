#!/usr/bin/env python3
"""Save the checked class-8 service evidence and the resulting startup boundary."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/system-query-checkpoint-20261003'
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
tests=R/'generated/integration-review/system-query'
for platform in ['macos','simulator']:
 p=tests/platform;d=json.loads((p/'result.json').read_text());assert d['passed']
 assert all(sha(R/n)==h for n,h in d['sources_sha256'].items())
 assert sha(p/'checked/result.json')==d['checked_sha256'] and sha(p/'guarded/result.json')==d['guarded_sha256']
 assert sha(p/'test-checked')==d['checked']['binary_sha256']
 if platform=='macos':assert d['normal']['passed'] and d['tsan']['passed']
tree(tests,'tests')
tree(R/'generated/integration-review/system-providers','provider-tests')
for platform in ['macos','device','simulator']:
 b=R/'generated/native-win32'/platform;d=json.loads((b/'result.json').read_text())
 assert d['objects']['guest_system_query']['source_sha256']==sha(R/'port/core/guest_system_query.c')
 for name in ['result.json','guest_system_query.o','native_system_providers.o','bindings.ll','bindings.o']:add(b/name,'builds/'+platform+'/'+name)
b=R/'generated/native-win32/checked-macos';d=json.loads((b/'result.json').read_text());assert d['state']=='finished' and not d['failures']
for name in ['result.json','guest_system_query.ll','guest_system_query.bc','guest_system_query.o']:add(b/name,'builds/checked/'+name)
native=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text())
assert run['state']=='finished' and run['initialized'] and run['string_patch_publication']
assert run['binary_sha256']==sha(native/'bootstrap') and run['external_build_sha256']==sha(native/'guarded/result.json')
assert len(run['ntdll_probe']['probes'])==6
assert all(p['name']!='NtQuerySystemInformation' for p in run['ntdll_probe']['probes'])
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name,'continuation/'+name)
for p in sorted(native.glob('*.ll')):add(p,'continuation/llvm/'+p.name)
source_names=['port/core/guest_system_query.c','port/core/guest_system_query.h','port/core/native_system_providers.inc','port/compiler/ExternalMemoryPass.cpp','port/llasm-runtime/win32-bindings.json','config/runtime/native-providers.json','config/runtime/external-memory-contracts.json','scripts/build-native-win32.py','scripts/guard_checked_link.py','scripts/test-system-query.py','scripts/test-qol-native-bootstrap.py','scripts/ntdll_probe_inputs.py','scripts/freeze-system-query.py','tests/system_query_test.c','tests/fixtures/system_query_dispatch.c','tests/fixtures/native_enter.ll','docs/archive/STATUS.md']
for name in source_names:add(R/name,'sources/'+name)
for name in ['system-query-tests.log','system-query-ipad.log','system-query-native-build.log','system-query-device-build.log','system-query-simulator-build.log','system-query-checked-build.log','system-query-external-regression.log','system-query-providers-test.log','system-query-native-continuation.log']:add(R/'generated/integration-review'/name,'logs/'+name)
add(R/'generated/tools/external-memory-pass/result.json','builds/external-pass.json')
add(R/'generated/integration-review/integer-runtime-checkpoint-20261003/checkpoint.json','dependencies/integer-runtime.json')
report=dict(created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=True,files=files,continuation=run['result'],exit=run['exit'],remaining_nt_probes=6,scope='Real class8 per-CPU counters through checked ABI on macOS and iPad Simulator component; device compilation only. Full original startup continues to the captured diagnostic boundary. No playable/menu/HD/authenticated-match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files')
