#!/usr/bin/env python3
"""Freeze CPU metadata policy, upper-address guard and actual startup evidence."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/cpu-profile-checkpoint-20261003'
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
for name in ['integration-review/registry/macos','integration-review/registry/simulator','integration-review/registry-frequency','native-win32/virtual-protect/macos','native-win32/protection-rollback','integration-review/patch-capture-memory']:
 p=R/'generated'/name;d=json.loads((p/'result.json').read_text());assert d.get('passed',d.get('pass'))
 for n,h in d.get('sources_sha256',d.get('source_sha256',{})).items():assert sha(R/n)==h,n
 tree(p,'tests/'+name)
for platform in ['macos','device','simulator']:
 p=R/'generated/native-win32'/platform;d=json.loads((p/'result.json').read_text())
 assert d['objects']['guest_registry']['source_sha256']==sha(R/'port/core/guest_registry.c')
 for n,h in d['cpu_metadata_sha256'].items():assert sha(R/n)==h
 for n in ['result.json','guest_registry.o','bindings.ll','bindings.o']:add(p/n,'builds/'+platform+'/'+n)
p=R/'generated/native-win32/checked-macos';d=json.loads((p/'result.json').read_text());assert d['state']=='finished' and not d['failures']
for n in ['result.json','guest_registry.ll','guest_registry.bc','guest_registry.o']:add(p/n,'builds/checked/'+n)
p=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((p/'result.json').read_text())
assert run['state']=='finished' and run['exit']==71 and run['result']['engine_entered']
assert run['result']['import']['name']=='SDL_HasMMX' and run['result']['attach_return']==1
assert len(run['registry_events'])==32 and all(e['result']==0 for e in run['registry_events'])
assert run['binary_sha256']==sha(p/'bootstrap') and run['external_build_sha256']==sha(p/'guarded/result.json')
assert len(run['ntdll_probe']['probes'])==6
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(p/n,'continuation/'+n)
for q in sorted(p.glob('*.ll')):add(q,'continuation/llvm/'+q.name)
names=['port/core/guest_registry.c','port/core/guest_registry.h','port/core/cpu_metadata_profile.h','port/core/guest_memory_checks.c','port/core/guest_memory_checks.h','port/core/guest_system.h','config/runtime/cpu-metadata.json','config/runtime/external-memory-contracts.json','scripts/gen-cpu-metadata.py','scripts/build-native-win32.py','scripts/build-checked-runtime.py','scripts/test-registry.py','scripts/test-registry-frequency.py','scripts/test-virtual-protect.py','scripts/test-qol-native-bootstrap.py','scripts/freeze-cpu-profile.py','tests/registry_test.c','tests/registry_frequency_test.c','tests/virtual_protect_test.c','tests/qol_native_bootstrap.c','docs/STATUS.md']
for n in names:add(R/n,'sources/'+n)
tree(R/'ref/downloads/libuv-cpu-reference','libuv-reference')
for q in sorted((R/'generated/integration-review').glob('cpu-profile-*.log')):add(q,'logs/'+q.name)
add(R/'generated/integration-review/registry-checkpoint-20261003/checkpoint.json','dependencies/registry.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation=run['result'],registry_events=run['registry_events'],scope='Declared 2400 MHz metadata fallback, not measured physical frequency. macOS/iPad component evidence; device compilation only. Original native startup clears the registry failure and reaches SDL_HasMMX. No playable, HD or multiplayer acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files')
