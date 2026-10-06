#!/usr/bin/env python3
"""Preserve the real SDL boundary and pinned implementation reference."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/sdl-hint-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
research=R/'generated/integration-review/sdl-hint-research';analysis=json.loads((research/'analysis.json').read_text());assert analysis['passed']
for p,h in analysis['inputs_sha256'].items():assert sha(R/p)==h,p
run=json.loads((native/'result.json').read_text());assert run['exit']==71 and run['result']['import']['name']=='SDL_SetHint'
assert run['binary_sha256']==sha(native/'bootstrap') and run['external_build_sha256']==sha(native/'guarded/result.json')
files=[]
def add(p,rel):
    dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    if not dst.exists():subprocess.run(['cp','-c',str(p),str(dst)],check=True)
    assert sha(p)==sha(dst),rel
    files.append(dict(original=str(p.relative_to(R)),path=rel,sha256=sha(dst)))
for name,row in run['import_checkpoint'].items():assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'capture/'+name)
for p in sorted(research.glob('*.json')):add(p,'analysis/'+p.name)
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','guarded/result.json','guarded/policy.json']:add(native/n,'native/'+n)
for p in sorted(native.glob('*.ll')):add(p,'native/llvm/'+p.name)
for n in ['scripts/analyze-sdl-hint.py','scripts/freeze-sdl-hint-capture.py','scripts/test-qol-native-bootstrap.py','tests/qol_native_bootstrap.c']:add(R/n,'sources/'+n)
refs=json.loads((research/'sources.json').read_text())
for row in refs['files']:assert sha(R/row['path'])==row['sha256'];add(R/row['path'],'reference/'+pathlib.Path(row['path']).name)
pin=json.loads((R/'ref/sdl3/source.json').read_text());assert sha(R/pin['archive_path'])==pin['sha256']
add(R/'ref/sdl3/source.json','reference/source.json');add(R/pin['archive_path'],'reference/SDL3-3.2.18.tar.gz')
add(R/'generated/integration-review/multimedia-timer-checkpoint-20261003/checkpoint.json','dependencies/timer.json')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,hint=analysis['hint'],value=analysis['value'],timer_caps=analysis['timer_caps'],
    scope='Captured SDL hint and retained timer capability output from original native startup. SDL3.2.18 upstream source pinned for exact semantic/reference/backend work. No hint implementation, guest allocator integration, window, renderer or gameplay claim.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; original SDL hint boundary')
