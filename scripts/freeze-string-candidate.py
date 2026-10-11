#!/usr/bin/env python3
"""Preserve the seventh unpublished candidate and its independent comparisons."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/string-candidate-checkpoint-20261003'
assert not (out/'checkpoint.json').exists(),'Immutable checkpoint already exists'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for data in iter(lambda:f.read(1024*1024),b''):h.update(data)
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
root=R/'generated/integration-review/string-patched-engine'
test=json.loads((root/'variants/test-result.json').read_text())
assert test['passed'] and test['cases']==5890 and not test['unvisited_targets']
assert sha(root/'variants/result.json')==test['variants_sha256']
assert sha(R/'scripts/test-string-patch-variants.py')==test['script_sha256']
assert all(sha(R/n)==h for n,h in test['support_sha256'].items())
for directory,key in [('string-trampolines','passed')]:
 assert json.loads((R/'generated/integration-review'/directory/'test-result.json').read_text())[key]
for directory,key in [('softfloat/stores','passed'),('softfloat/runtime','pass'),('softfloat/stack','pass'),('bochs-math/checked-tests','passed')]:
 assert json.loads((R/'generated'/directory/'result.json').read_text())[key]
tree(root,'candidate');tree(R/'generated/integration-review/string-trampolines','trampolines')
for name in ['stores','runtime','stack']:tree(R/'generated/softfloat'/name,'math-tests/'+name)
tree(R/'generated/bochs-math/checked','checked-math')
tree(R/'generated/bochs-math/checked-tests','math-tests/checked')
for platform in ['macos','device','simulator']:
 b=R/'generated/bochs-math';b=b if platform=='macos' else b/platform
 for name in ['result.json','math_bridge.o','libbochs-math.a']+(['libmath.dylib'] if platform=='macos' else []):add(b/name,'builds/'+platform+'/bochs/'+name)
 for name in ['result.json','tribes_float80.o','softfloat.a']:add(R/'generated/softfloat'/platform/name,'builds/'+platform+'/softfloat/'+name)
for name in ['prepare-string-patches.py','prepare-string-overlaps.py','prepare-string-patch-variants.py','prepare-string-trampolines.py','test-string-patch-variants.py','test-string-trampolines.py','test-float80-stores.py','test-float80-runtime.py','test-float80-stack.py','test-checked-bochs.py','build-bochs-math.py','build-checked-bochs.py','build-softfloat.py','freeze-string-candidate.py','prepare-va-model.py']:
 add(R/'scripts'/name,'sources/scripts/'+name)
for name in ['port/bochs-support/math_bridge.cc','port/bochs-support/config.h','port/core/bochs_math.h','port/core/ext80.c','port/core/ext80.h','port/llasm-support/tribes_float80.c','port/llasm-support/llasm_cpu.h','tests/float80_store_oracle.c','tests/string_clock_x86_oracle.c','tests/x87_result_slot.c','docs/archive/STATUS.md','dependencies.lock.json','toolchains.lock.json']:
 add(R/name,'sources/'+name)
for name in ['string-patch-compile.log','string-overlap-compile.log','string-variants-compile.log','string-variant-tests.log','string-store-tests.log','string-float-runtime-tests.log','string-float-stack-tests.log','string-checked-bochs-build.log','string-checked-bochs-tests.log']:
 add(R/'generated/integration-review'/name,'logs/'+name)
for name in ['http-publication-checkpoint-20261003','http-patch-candidate-checkpoint-20261002']:
 add(R/'generated/integration-review'/name/'checkpoint.json','dependencies/'+name+'.json')
report=dict(created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=True,files=files,native_seventh_phase_verified=False,published=False,block_cases=5890,block_definitions=92,trampoline_cases=768,store_oracle_cases=31688,scope='Strict seventh string/utility candidate and isolated ARM64/x86 block comparisons. Float/double store C1 repair validated independently under Rosetta. No seventh native publication, full startup, app launch, rendering or authenticated match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files;',sum((out/r['path']).stat().st_size for r in files),'logical bytes')
