#!/usr/bin/env python3
"""Preserve independent integer-store proof and the next original-startup boundary."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/integer-runtime-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
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
reference=R/'generated/softfloat/integer-stores';proof=json.loads((reference/'result.json').read_text());assert proof['passed'] and proof['cases']==33225 and proof['emitted_instruction_cases']==8307
assert proof['reference_cases_sha256']==sha(reference/'reference-cases.txt')
assert all(sha(R/n)==h for n,h in proof['sources_sha256'].items())
sim=json.loads((reference/'simulator/result.json').read_text());assert sim['passed'] and sim['reference_sha256']==sha(reference/'result.json')
guard=R/'generated/bochs-math/checked-tests';gm=json.loads((guard/'result.json').read_text());assert gm['passed'] and gm['equivalence_cases']['store_integer']==1472 and len(gm['protected_output_faults'])==13
assert gm['checked_build_sha256']==sha(R/'generated/bochs-math/checked/result.json')
tree(reference,'integer-tests');tree(guard,'guarded-math-tests');tree(R/'generated/bochs-math/checked','checked-math')
variants=R/'generated/integration-review/string-patched-engine/variants';vm=json.loads((variants/'test-result.json').read_text());assert vm['passed'] and all(sha(R/n)==h for n,h in vm['support_sha256'].items())
for name in ['test-result.json','test.dylib','isolated.ll','test-bridge.ll','clock-x86-oracle','clock-x86-oracle.log','clock-input.bin']:add(variants/name,'string-regression/'+name)
for platform in ['macos','device','simulator']:
 b=R/'generated/bochs-math';b=b if platform=='macos' else b/platform
 for name in ['result.json','math_bridge.o','libbochs-math.a']+(['libmath.dylib'] if platform=='macos' else []):add(b/name,'builds/'+platform+'/bochs/'+name)
 for name in ['result.json','tribes_float80.o','softfloat.a']:add(R/'generated/softfloat'/platform/name,'builds/'+platform+'/softfloat/'+name)
native=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text())
assert run['initialized'] and run['string_patch_publication'] and run['state']=='finished'
assert sha(native/'bootstrap')==run['binary_sha256'] and sha(native/'guarded/result.json')==run['external_build_sha256']
assert sha(native/'support/result.json')==run['checked_support_build_sha256']
assert run['source_sha256']['port/llasm-support/tribes_float80.c']==sha(R/'port/llasm-support/tribes_float80.c')
for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name,'continuation/'+name)
for p in sorted(native.glob('*.ll')):add(p,'continuation/llvm/'+p.name)
for name in ['port/bochs-support/math_bridge.cc','port/core/bochs_math.h','port/llasm-support/tribes_float80.c','tests/float80_integer_store_oracle.c','tests/float80_integer_component.c','tests/x87_result_slot.c','scripts/test-float80-integer-stores.py','scripts/test-float80-integer-platform.py','scripts/test-checked-bochs.py','scripts/test-string-patch-variants.py','scripts/test-qol-native-bootstrap.py','scripts/freeze-integer-runtime.py','docs/STATUS.md']:
 add(R/name,'sources/'+name)
for name in ['integer-store-tests.log','integer-ipad-component.log','integer-checked-bochs-tests.log','integer-string-variant-tests.log','integer-native-continuation.log']:add(R/'generated/integration-review'/name,'logs/'+name)
for name in ['integer-conversion-checkpoint-20261003','string-publication-checkpoint-20261003']:add(R/'generated/integration-review'/name/'checkpoint.json','dependencies/'+name+'.json')
report=dict(created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=True,files=files,integer_helper_cases=33225,emitted_instruction_cases=8307,simulator=sim['device']['name'],continuation_result=run['result'],continuation_exit=run['exit'],scope='Checked signed16/32/64 integer stores; independent x86-64/Rosetta numeric/status/stack proof, emitted FISTP qword at0x427d37 and iPad Simulator command-line vectors. Native original-startup continuation retained separately; no menu, renderer, physical-device or authenticated-match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files;',sum((out/r['path']).stat().st_size for r in files),'logical bytes')
