#!/usr/bin/env python3
"""Freeze verified per-device AOT immediates and the next original audio gate."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/display-immediates-checkpoint-20261003';assert not (out/'checkpoint.json').exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();files={}
def add(p):
 p=p.resolve();rel=str(p.relative_to(R))
 if rel in files:return
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);subprocess.run(['cp','-c',str(p),str(dst)],check=True);assert sha(p)==sha(dst);files[rel]=sha(dst)
def sources(d,key):
 for n,h in d[key].items():assert sha(R/n)==h,n;add(R/n)
def tree(p):
 for f in p.rglob('*'):
  if f.is_file() and '__pycache__' not in f.parts:add(f)
root=R/'generated/integration-review/display-immediates-candidate';prep=json.loads((root/'preparation.json').read_text());test=json.loads((root/'test-result.json').read_text());proof=json.loads((root/'native-comparison.json').read_text());assert prep['passed'] and test['passed'] and proof['passed'];assert test['cases']==512 and proof['guest_bytes_compared']==20480 and not proof['cpu_differences'] and not proof['memory_differences'];assert len(proof['writes'])==2 and len(proof['services'])==1
assert test['preparation_sha256']==sha(root/'preparation.json');sources(prep,'inputs_sha256');sources(test,'sources_sha256');sources(proof,'sources_sha256')
for d in [prep,test]:
 for n,h in d['outputs_sha256'].items():assert sha(root/n)==h;add(root/n)
tree(root)
for name in ['code-revision','display-immediates-routes']:
 p=R/'generated/integration-review'/name;d=json.loads((p/'result.json').read_text());assert d['passed'];assert sha(p/'test')==d['binary_sha256'];add(p/'test');add(p/'result.json')
 if 'sources_sha256' in d:sources(d,'sources_sha256')
 else:assert d['harness_sha256']==sha(R/'tests/qol_native_bootstrap.c') and d['script_sha256']==sha(R/'scripts/test-display-immediates-routes.py');assert (d['checks'],d['expected_faults'],d['routes'])==(8112,548,332)
 for f in p.glob('*.log'):add(f)
for name in ['macos','B8138F85-0AE9-4494-9564-EDC7FD4E3B7F']:
 p=R/'generated/integration-review/code-revision-threads'/name;d=json.loads((p/'result.json').read_text());assert d['passed'];sources(d,'sources_sha256');add(p/'result.json')
 for row in d['runs']:assert sha(p/row['mode'])==row['binary_sha256'];add(p/row['mode']);add(p/(row['mode']+'.log'))
 p=R/'generated/integration-review/sdl-display'/name;d=json.loads((p/'result.json').read_text());assert d['passed'];sources(d,'sources_sha256');add(p/'result.json')
 for row in d['runs']:
  app=R/row['app'];exe=app/('Contents/MacOS/SDLDisplayProbe' if name=='macos' else 'SDLDisplayProbe');assert sha(exe)==row['binary_sha256'];tree(app)
  if row['mode']=='checked':assert len(row['faults'])==12
 for f in p.glob('*.log'):add(f)
n=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';d=json.loads((n/'result.json').read_text());assert d['initialized'] and d['exit']==-6 and d['result']['reason']=='SDL subsystem initialization flags';assert d['binary_sha256']==sha(n/'bootstrap')==proof['binary_sha256'];sources(d,'source_sha256')
assert 'Published display immediates after two native-derived stores' in d['stderr'];assert d['stderr'].count('Validated display immediate write')==2
routes=json.loads((R/'generated/integration-review/display-immediates-routes/result.json').read_text());assert routes['engine_sha256']==sha(n/'engine-routed.ll')
for f in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','display-immediates-inputs.json','display_immediates_publication.inc','display-immediates-engine.ll','engine-routed.ll','qol-patched.ll','engine.bin','qol.bin','guarded/result.json','guarded/policy.json','support/result.json']:add(n/f)
g=json.loads((n/'guarded/result.json').read_text());assert g['plugin_build_sha256']==sha(R/'generated/tools/external-memory-pass/result.json') and g['config_sha256']==sha(R/'config/runtime/external-memory-contracts.json')
nextroot=R/'generated/integration-review/sdl-next-subsystem';nextproof=json.loads((nextroot/'analysis.json').read_text());assert nextproof['passed'] and nextproof['subsystem']=='SDL_INIT_AUDIO' and nextproof['binary_sha256']==d['binary_sha256'];sources(nextproof,'sources_sha256');tree(nextroot)
p=R/'generated/integration-review/sdl-video/platforms/result.json';platforms=json.loads(p.read_text());assert platforms['passed'];add(p)
for row in platforms['rows']:
 assert sha(R/row['source'])==row['source_sha256'] and sha(R/row['object'])==row['object_sha256'];add(R/row['object'])
for f in ['scripts/display_immediates_runtime_inputs.py','scripts/test-display-immediates-routes.py','scripts/test-code-revision.py','scripts/freeze-display-immediates.py','port/core/guest_memory_checks.h','config/runtime/display-immediates.json','config/runtime/sdl-display.json','config/runtime/sdl-bindings.json','config/runtime/external-memory-contracts.json','generated/tools/external-memory-pass/result.json','docs/STATUS.md','docs/EXECUTION-PLAN.md']:add(R/f)
report=dict(passed=True,goal_complete=False,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,native_revision=18,binary_sha256=d['binary_sha256'],continuation_result=d['result'],scope='Original native display resolution writes validated and activated using precompiled MOV variants with per-device data. Original bounded execution agrees with x86; actual concurrent prior routing and SDL lifecycle pass Mac/TSan/sole iPad. Next stop is original AUDIO initialization. No window, HD, gameplay or authenticated match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for n,h in files.items():assert sha(out/n)==h
print('PASS:',len(files),'source-verified files frozen;18th revision; game remains unplayable')
