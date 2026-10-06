#!/usr/bin/env python3
"""Freeze TLS component acceptance and the failing original continuation separately."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-tls-checkpoint-20261004';assert not (out/'checkpoint.json').exists();files={}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def add(p):
 p=p.resolve();rel=str(p.relative_to(R));dest=out/rel
 if rel in files:assert files[rel]==sha(p);return
 dest.parent.mkdir(parents=True,exist_ok=True);subprocess.run(['cp','-c',str(p),str(dest)],check=True);assert sha(dest)==sha(p);files[rel]=sha(dest)
def source_map(d,key):
 for n,h in d.get(key,{}).items():assert sha(R/n)==h,n;add(R/n)
sim='B8138F85-0AE9-4494-9564-EDC7FD4E3B7F'
for family in ['sdl-tls','guest-threads','sdl-audio']:
 for platform in ['macos',sim]:
  root=R/'generated/integration-review'/family/platform;d=json.loads((root/'result.json').read_text());assert d['passed'];source_map(d,'sources_sha256');add(root/'result.json')
  for row in d['runs']:
   p=R/row['app']/('Contents/MacOS' if platform=='macos' else '')/'SDLAudioProbe' if family=='sdl-audio' else root/('test-'+row['mode'])
   assert sha(p)==row['binary_sha256'];add(p)
  for p in root.glob('*.log'):add(p)
n=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';d=json.loads((n/'result.json').read_text());assert d['initialized'] and d['exit']==-6 and d['result']['kind']=='memory_violation' and '0x410c79' in d['result']['reason'];assert sha(n/'bootstrap')==d['binary_sha256'];source_map(d,'source_sha256')
for name in ['bootstrap','engine.bin','qol.bin','result.json','stdout.log','stderr.log','support/result.json','guarded/result.json','guarded/policy.json']:add(n/name)
for root in [R/'generated/native-win32/macos',R/'generated/native-win32/checked-macos']:
 add(root/'result.json')
meta=json.loads((R/'generated/native-win32/checked-macos/result.json').read_text())
for row in meta['objects'].values():assert sha(R/row['source'])==row['source_sha256'];add(R/row['source'])
e=R/'generated/integration-review/sdl-tls-execution';captures=json.loads((e/'capture.json').read_text());assert [v['eax'] for v in captures]==[1,0xd8000001]
for c in captures:assert sha(e/(c['function']+'.cpu'))==c['cpu_sha256']
for p in e.iterdir():
 if p.is_file():add(p)
for p in (R/'generated/integration-review/sdl-audio-consumers').glob('*.json'):add(p)
for name in ['README.md','docs/STATUS.md','docs/EXECUTION-PLAN.md','docs/EXECUTION-HISTORY.md','docs/PROJECT-REVIEW-2026-10-03.md','config/runtime/sdl-tls.json','config/runtime/sdl-native-ownership.json','port/core/guest_threads.h','scripts/freeze-sdl-tls.py','scripts/sdl_backend_inputs.py','generated/tools/external-memory-pass/result.json','generated/integration-review/external-memory-pass/result.json','generated/integration-review/sdl-native-ownership-checkpoint-20261004/checkpoint.json']:add(R/name)
(out/'checkpoint.json').write_text(json.dumps(dict(component_tests_passed=True,original_startup_passed=False,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,binary_sha256=d['binary_sha256'],continuation=d['result'],captures=captures,scope='Original TLS/mutex returns verified; full startup still aborts on an unadmitted code patch. No19th revision, original window, mixer, map, HD or authenticated match acceptance.'),indent=2)+'\n');print('PASS:',len(files),'source-verified files frozen; original startup remains failed')
