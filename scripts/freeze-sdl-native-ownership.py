#!/usr/bin/env python3
"""Preserve the ownership review's actual sources, failures and successful audio evidence."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-native-ownership-checkpoint-20261004';assert not (out/'checkpoint.json').exists();files={}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def add(p):
 p=p.resolve();rel=str(p.relative_to(R));dest=out/rel
 if rel in files:assert files[rel]==sha(p);return
 dest.parent.mkdir(parents=True,exist_ok=True);subprocess.run(['cp','-c',str(p),str(dest)],check=True);assert sha(p)==sha(dest);files[rel]=sha(dest)
def source_map(d,key):
 for name,h in d.get(key,{}).items():assert sha(R/name)==h,name;add(R/name)
sim='B8138F85-0AE9-4494-9564-EDC7FD4E3B7F'
for name in ['sdl-audio/macos','sdl-audio/'+sim,'sdl-display/macos','sdl-display/'+sim,'sdl-sync/macos','sdl-sync/'+sim,'sdl-hints']:
 root=R/'generated/integration-review'/name;d=json.loads((root/'result.json').read_text());assert d['passed'];add(root/'result.json');source_map(d,'sources_sha256')
 for row in d.get('runs',[]):
  if 'app' in row:
   app=R/row['app'];exe=app/('Contents/MacOS' if name.endswith('/macos') else '')/('SDLAudioProbe' if 'audio' in name else 'SDLDisplayProbe')
  else:exe=root/('test-'+row['mode'])
  assert sha(exe)==row['binary_sha256'];add(exe)
 if name=='sdl-hints':
  for mode in ['normal','tsan','checked']:
   exe=root/('test-'+mode);assert sha(exe)==d[mode]['binary_sha256'];add(exe)
 for p in root.glob('*.log'):add(p)
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';d=json.loads((native/'result.json').read_text());assert d['state']=='finished' and d['initialized'];assert d['result']['import']['name']=='SDL_SetTLS' and d['result']['eax']==1;assert sha(native/'bootstrap')==d['binary_sha256'];source_map(d,'source_sha256')
for name in ['bootstrap','engine.bin','qol.bin','result.json','stdout.log','stderr.log','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name)
e=R/'generated/integration-review/native-sdl-audio-execution';capture=json.loads((e/'capture.json').read_text());assert capture['eax']==1 and capture['cpu_sha256']==sha(e/'cpu.bin');assert '"coreaudio"' in (e/'audio-lldb.log').read_text()
for p in e.rglob('*'):
 if p.is_file():add(p)
for name in ['audio-baseline-reproduction','audio-native-ownership']:
 root=R/'generated/integration-review'/name
 for p in root.iterdir():
  if p.is_file() and p.suffix in ['.json','.log','.c']:add(p)
for name in ['docs/STATUS.md','docs/PROJECT-REVIEW-2026-10-03.md','docs/EXECUTION-PLAN.md','README.md','config/runtime/sdl-native-ownership.json','scripts/freeze-sdl-native-ownership.py','generated/integration-review/display-immediates-checkpoint-20261003/checkpoint.json']:add(R/name)
(out/'checkpoint.json').write_text(json.dumps(dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,binary_sha256=d['binary_sha256'],original_audio_return=capture,continuation=d['result'],scope='Real original audio initialization and native component silent playback. SDL TLS is the next import boundary. No original window/map/audio mixing/HD/authenticated multiplayer acceptance.'),indent=2)+'\n')
print('PASS:',len(files),'source-verified files frozen')
