#!/usr/bin/env python3
"""Preserve SDL guest feature-query tests and the following native boundary."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/sdl-cpu-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
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
for platform in ['macos','simulator']:
 p=R/'generated/integration-review/sdl-cpu'/platform;d=json.loads((p/'result.json').read_text());assert d['passed']
 for n,h in d['sources_sha256'].items():assert sha(R/n)==h,n
 for mode in (['checked','normal','tsan'] if platform=='macos' else ['checked']):assert d[mode]['passed'] and sha(p/('test-'+mode))==d[mode]['binary_sha256']
 assert sha(p/'checked/result.json')==d['checked_sha256'] and sha(p/'guarded/result.json')==d['guarded_sha256']
 tree(p,'tests/'+platform)
p=R/'generated/integration-review/processor-features';d=json.loads((p/'result.json').read_text());assert d['passed']
for n,h in d['source_sha256'].items():assert sha(R/n)==h,n
tree(p,'tests/windows-features')
for platform in ['macos','device','simulator']:
 p=R/'generated/native-win32'/platform;d=json.loads((p/'result.json').read_text())
 assert d['objects']['guest_processor_features']['source_sha256']==sha(R/'port/core/guest_processor_features.c')
 for n in ['result.json','guest_cpu.o','guest_processor_features.o','bindings.ll','bindings.o']:add(p/n,'builds/'+platform+'/'+n)
p=R/'generated/native-win32/checked-macos';d=json.loads((p/'result.json').read_text());assert d['state']=='finished' and not d['failures']
for n in ['result.json','guest_processor_features.ll','guest_processor_features.bc','guest_processor_features.o']:add(p/n,'builds/checked/'+n)
p=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((p/'result.json').read_text())
assert run['state']=='finished' and run['exit']==71 and run['result']['engine_entered'] and run['result']['attach_return']==1
assert run['result']['import']['name']=='SetPriorityClass' and run['result']['stack_arguments'][:2]==[0xffffffff,0x8000]
assert len(run['registry_events'])==32 and all(e['result']==0 for e in run['registry_events'])
assert run['binary_sha256']==sha(p/'bootstrap') and run['external_build_sha256']==sha(p/'guarded/result.json')
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(p/n,'continuation/'+n)
for q in sorted(p.glob('*.ll')):add(q,'continuation/llvm/'+q.name)
names=['port/core/guest_cpu.c','port/core/guest_cpu.h','port/core/guest_processor_features.c','port/core/guest_processor_features.h','port/llasm-runtime/win32-bindings.json','config/runtime/cpu.json','config/runtime/sdl-cpu-reference.json','scripts/test-sdl-cpu.py','scripts/test-processor-features.py','scripts/gen-win32-bindings.py','scripts/freeze-sdl-cpu.py','tests/sdl_cpu_test.c','tests/fixtures/sdl_cpu_dispatch.c','tests/fixtures/native_enter.ll','docs/archive/STATUS.md']
for n in names:add(R/n,'sources/'+n)
for row in json.loads((R/'config/runtime/sdl-cpu-reference.json').read_text())['references']:add(R/row['path'],'references/'+pathlib.Path(row['path']).name)
add(R/'ref/downloads/SDL-release-3.2.18-LICENSE.txt','references/LICENSE.txt')
for q in sorted((R/'generated/integration-review').glob('sdl-cpu-*.log')):add(q,'logs/'+q.name)
add(R/'generated/integration-review/cpu-profile-checkpoint-20261003/checkpoint.json','dependencies/cpu-profile.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation=run['result'],scope='MMX/SSE/AVX SDL queries follow the fixed x86 guest profile. macOS and sole iPad Simulator component execution; all Apple target builds. Original native startup advances to SetPriorityClass. No menu, rendering, HD or authenticated-match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files')
