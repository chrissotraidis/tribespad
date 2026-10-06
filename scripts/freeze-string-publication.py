#!/usr/bin/env python3
"""Preserve seventh native publication proof before continuing mutable startup."""
import datetime,hashlib,json,pathlib,subprocess,sys
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/string-publication-checkpoint-20261003'
native=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
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
def snapshot(namespace):
 d=json.loads((native/'result.json').read_text())
 assert sha(native/'bootstrap')==d['binary_sha256'] and sha(native/'guarded/result.json')==d['external_build_sha256']
 assert sha(native/'support/result.json')==d['checked_support_build_sha256']
 for name in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','string-patch-inputs.json','string_publication.inc','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name,namespace+'/'+name)
 for p in sorted(native.glob('*.ll')):add(p,namespace+'/llvm/'+p.name)
 for key in ['string_patch_checkpoint','string_patch_final','patch_snapshots']:
  for name,row in d.get(key,{}).items():
   p=R/row['path'];assert sha(p)==row['sha256'];add(p,namespace+'/'+name)
 return d
if sys.argv[1]=='proof':
 assert not (out/'proof-files.json').exists()
 d=snapshot('native-proof');assert d['result']['kind']=='string_patch_native_return' and d['string_patch_publication']
 comparison=R/'generated/integration-review/native-string-prefix-test'
 checked=json.loads((comparison/'result.json').read_text())
 assert checked['passed'] and checked['run_sha256']==sha(native/'result.json')
 assert checked['script_sha256']==sha(R/'scripts/test-native-string-patch.py')
 tree(comparison,'comparison')
 for name,rel in [('string-routes','routes'),('string-writers','writers'),('code-revision','guard-revisions'),('published-code','guard-publication'),('string-patch-order','write-flush-order')]:tree(R/'generated/integration-review'/name,rel)
 for name in ['scripts/string_patch_runtime_inputs.py','scripts/record-string-patch-order.py','scripts/patch_runtime_inputs.py','scripts/test-qol-native-bootstrap.py','scripts/test-native-string-patch.py','scripts/test-string-routes.py','scripts/test-string-writers.py','scripts/test-code-revision.py','scripts/freeze-string-publication.py','tests/qol_native_bootstrap.c','tests/code_revision_test.c','port/core/guest_memory_checks.c','port/core/guest_memory_checks.h']:
  add(R/name,'sources/'+name)
 for name in ['string-native-verification.log','string-native-comparison.log','string-route-tests.log','string-writer-tests.log','string-code-revision-tests.log','string-publication-tests.log']:add(R/'generated/integration-review'/name,'logs/'+name)
 for name in ['http-publication-checkpoint-20261003','string-candidate-checkpoint-20261003']:add(R/'generated/integration-review'/name/'checkpoint.json','dependencies/'+name+'.json')
 (out/'proof-files.json').write_text(json.dumps(files,indent=2)+'\n')
 for row in files:assert sha(out/row['path'])==row['sha256']
 assert sha(out/'comparison/result.json')==sha(comparison/'result.json')
 print('PASS: saved native proof and comparison:',len(files),'files')
else:
 assert sys.argv[1]=='finish' and not (out/'checkpoint.json').exists()
 files=json.loads((out/'proof-files.json').read_text());d=snapshot('continuation')
 assert d['string_patch_publication'] and d['result']['kind']!='string_patch_native_return'
 add(R/'docs/STATUS.md','sources/docs/STATUS.md')
 add(R/'generated/integration-review/string-native-continuation.log','logs/string-native-continuation.log')
 report=dict(created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),passed=True,files=files,native_string_phase_verified=True,continuation_result=d['result'],continuation_exit=d['exit'],continuation_is_boundary_only=True,scope='Seventh finite startup revision executes natively with114 ordered stores and12 intermediate flushes before route activation. Whole phase agrees with original x86 across CPU, permissions and17874944 readable bytes.2352 route checks,215 expected stops; guard tests reject21 ordinary writes; shared writer tests reject20 invalid writes. New bank records remain unpublished. Subsequent startup boundary retained separately. Diagnostic NT probes remain; no app/gameplay/HD/authenticated match acceptance.')
 (out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
 for row in files:assert sha(out/row['path'])==row['sha256']
 print('PASS: frozen and verified',len(files),'files;',sum((out/r['path']).stat().st_size for r in files),'logical bytes')
