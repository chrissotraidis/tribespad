#!/usr/bin/env python3
"""Freeze verified17th publication, concurrent routing, and the next actual stop."""
import datetime,hashlib,json,pathlib,re,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/post-video-tail-checkpoint-20261003';assert not (out/'checkpoint.json').exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();files={}
def add(p):
 p=p.resolve();rel=p.relative_to(R)
 if str(rel) in files:return
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);subprocess.run(['cp','-c',str(p),str(dst)],check=True);assert sha(p)==sha(dst);files[str(rel)]=sha(dst)
def sources(d,key):
 for n,h in d[key].items():assert sha(R/n)==h,n;add(R/n)
def binary(p,h):assert sha(p)==h,p;add(p)
root=R/'generated/integration-review/post-video-tail-candidate';prep=json.loads((root/'preparation.json').read_text());test=json.loads((root/'test-result.json').read_text());proof=json.loads((root/'native-comparison.json').read_text());assert prep['passed'] and test['passed'] and proof['passed'];assert test['cases']==768 and proof['guest_bytes_compared']==20480 and not proof['cpu_differences'] and not proof['memory_differences']
assert test['preparation_sha256']==sha(root/'preparation.json');sources(prep,'inputs_sha256');sources(test,'sources_sha256');sources(proof,'sources_sha256')
for d in [prep,test]:
 for n,h in d['outputs_sha256'].items():binary(root/n,h)
for n in ['preparation.json','test-result.json','native-comparison.json']:add(root/n)
for sub in ['execution']:
 for p in (root/sub).rglob('*'):
  if p.is_file() and p.suffix in ['.json','.log','.py','.bin','.c'] or p.is_file() and p.name=='bootstrap':add(p)
cap=json.loads((root/'execution/execution.json').read_text())
for row in cap['endpoints'].values():
 binary(root/'execution'/row['cpu_file'],row['cpu_sha256'])
 for page in row['ranges']:binary(root/'execution'/page['file'],page['sha256'])
for name in ['code-revision','post-video-tail-routes']:
 p=R/'generated/integration-review'/name;d=json.loads((p/'result.json').read_text());assert d['passed'];binary(p/'test',d['binary_sha256']);add(p/'result.json')
 if 'sources_sha256' in d:sources(d,'sources_sha256')
 else:assert d['harness_sha256']==sha(R/'tests/qol_native_bootstrap.c') and d['script_sha256']==sha(R/'scripts/test-post-video-tail-routes.py');assert d['checks']==7411 and d['expected_faults']==502 and d['routes']==330
 for f in p.glob('*.log'):add(f)
for name in ['macos','B8138F85-0AE9-4494-9564-EDC7FD4E3B7F']:
 p=R/'generated/integration-review/code-revision-threads'/name;d=json.loads((p/'result.json').read_text());assert d['passed'];sources(d,'sources_sha256');add(p/'result.json')
 for row in d['runs']:binary(p/row['mode'],row['binary_sha256']);add(p/(row['mode']+'.log'))
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';d=json.loads((native/'result.json').read_text());assert d['initialized'] and d['exit']==71 and d['result']['kind']=='import' and d['result']['import']['name']=='SDL_GetPrimaryDisplay';sources(d,'source_sha256');binary(native/'bootstrap',d['binary_sha256']);assert proof['binary_sha256']==d['binary_sha256'];binary(native/'guarded/result.json',d['external_build_sha256'])
assert 'Published post-video tail after three exact stores' in d['stderr'];assert len(re.findall('Validated post-video tail write',d['stderr']))==3
assert all(d[k] for k in ['checked_image','checked_runtime_objects','checked_support_objects','external_call_gates','independent_subpages_enabled'])
routes=json.loads((R/'generated/integration-review/post-video-tail-routes/result.json').read_text());assert routes['engine_sha256']==sha(native/'engine-routed.ll')
for n in ['result.json','stdout.log','stderr.log','patch-inputs.json','post-video-tail-inputs.json','post_video_tail_publication.inc','post-video-tail-engine.ll','engine-routed.ll','qol-patched.ll','engine.bin','qol.bin','guarded/policy.json','support/result.json']:add(native/n)
g=json.loads((native/'guarded/result.json').read_text());assert g['plugin_build_sha256']==sha(R/'generated/tools/external-memory-pass/result.json') and g['config_sha256']==sha(R/'config/runtime/external-memory-contracts.json')
nextroot=R/'generated/integration-review/sdl-display-entry';nextproof=json.loads((nextroot/'analysis.json').read_text());assert nextproof['passed'] and nextproof['entry_import']=='SDL_GetPrimaryDisplay' and nextproof['binary_sha256']==d['binary_sha256'];sources(nextproof,'sources_sha256')
for p in nextroot.iterdir():
 if p.is_file() and p.suffix in ['.json','.py','.bin','.log']:add(p)
for n in ['scripts/post_video_tail_runtime_inputs.py','scripts/test-post-video-tail-routes.py','scripts/test-code-revision.py','scripts/freeze-post-video-tail.py','port/core/guest_memory_checks.h','config/runtime/post-video-tail.json','generated/tools/external-memory-pass/result.json','generated/integration-review/byte-cas-checkpoint-20261003/checkpoint.json','docs/STATUS.md','docs/EXECUTION-PLAN.md']:add(R/n)
report=dict(passed=True,goal_complete=False,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,native_revision=17,continuation_result=d['result'],scope='Three post-video tail writes and compiled CALL/PUSH variants verified, prior routes preserved through actual concurrent publication. Actual bounded native/x86 prefix matches. Full client passes subsequent registrations and stops at unimplemented SDL_GetPrimaryDisplay; actual import entry captured without a substituted result. No window, HD, gameplay or authenticated match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for n,h in files.items():assert sha(out/n)==h
print('PASS:',len(files),'source-verified files frozen;17th revision; goal remains active')
