#!/usr/bin/env python3
"""Freeze source-verified Apple SDL video lifecycle and original startup evidence."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-video-checkpoint-20261003';assert not (out/'checkpoint.json').exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();files={}
def add(p):
 p=p.resolve();rel=p.relative_to(R);dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if str(rel) in files:return
 subprocess.run(['cp','-c',str(p),str(dst)],check=True);assert sha(p)==sha(dst);files[str(rel)]=sha(dst)
def source_map(d,key):
 for name,h in d[key].items():assert sha(R/name)==h,name;add(R/name)
for suffix in ['macos','B8138F85-0AE9-4494-9564-EDC7FD4E3B7F']:
 root=R/'generated/integration-review/sdl-video'/suffix;d=json.loads((root/'result.json').read_text());assert d['passed'];add(root/'result.json');source_map(d,'sources_sha256')
 for run in d['runs']:
  app=R/run['app'];binary=app/('Contents/MacOS/SDLVideoProbe' if suffix=='macos' else 'SDLVideoProbe');assert sha(binary)==run['binary_sha256'];add(binary);add(app/('Contents/Info.plist' if suffix=='macos' else 'Info.plist'))
 for p in root.iterdir():
  if p.is_file() and p.suffix=='.log':add(p)
 root=R/'generated/integration-review/sdl-video/error-oom'/suffix;d=json.loads((root/'result.json').read_text());assert d['passed'];add(root/'result.json');source_map(d,'source_sha256')
 for run in d['runs']:
  p=root/run['mode'];assert sha(p)==run['binary_sha256'];add(p);add(root/(run['mode']+'.log'))
 root=R/'generated/integration-review/sdl-hints';root=root/suffix if suffix!='macos' else root;d=json.loads((root/'result.json').read_text());assert d['passed'];add(root/'result.json');source_map(d,'sources_sha256')
 for mode in ['normal','tsan','checked'] if suffix=='macos' else ['checked']:
  p=root/('test-'+mode);assert sha(p)==d[mode]['binary_sha256'];add(p);add(root/(mode+'.log'))
for platform in ['macos','device','simulator']:
 root=R/'generated/sdl-backend'/platform;d=json.loads((root/'result.json').read_text());assert d['passed'] and d['config_sha256']==sha(R/'config/runtime/sdl-backend.json') and d['script_sha256']==sha(R/'scripts/build-sdl-backend.py');add(root/'result.json')
 for name,h in d['libraries'].items():assert sha(R/name)==h,name;add(R/name)
 for name,h in d['patches'].items():assert sha(root/'source'/name)==h,name;add(root/'source'/name)
root=R/'generated/integration-review/sdl-video/platforms';d=json.loads((root/'result.json').read_text());assert d['passed'];add(root/'result.json');assert d['script_sha256']==sha(R/'scripts/test-sdl-video-platforms.py');add(R/'scripts/test-sdl-video-platforms.py')
for row in d['rows']:
 assert sha(R/row['source'])==row['source_sha256'];assert sha(R/row['object'])==row['object_sha256'];add(R/row['object'])
root=R/'generated/integration-review/external-memory-pass';d=json.loads((root/'result.json').read_text());assert d['pass_'];add(root/'result.json');source_map(d,'sources_sha256');source_map(d,'plugins');assert sha(root/'test')==d['binary_sha256'];add(root/'test');add(root/'run.log')
native=R/'generated/srw/run-20261003T150627-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';d=json.loads((native/'result.json').read_text());assert d['state']=='finished' and d['initialized'] and sha(native/'bootstrap')==d['binary_sha256'];source_map(d,'source_sha256')
g=json.loads((native/'guarded/result.json').read_text());assert g['state']=='compiled' and g['config_sha256']==sha(R/'config/runtime/external-memory-contracts.json') and g['plugin_build_sha256']==sha(R/'generated/tools/external-memory-pass/result.json')
for name in ['bootstrap','engine.bin','qol.bin','result.json','stdout.log','stderr.log','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name)
evidence=R/'generated/integration-review/sdl-video';capture=json.loads((evidence/'video-startup.json').read_text());assert capture['binary_sha256']==d['binary_sha256'];assert capture['log_sha256']==sha(evidence/'video-startup-lldb.log');assert capture['video_returns'][0]['native_result']==1
for p in evidence.iterdir():
 if p.is_file() and p.suffix in ['.json','.log','.py']:add(p)
for p in (evidence/'error-recursion').iterdir():
 if p.is_file():add(p)
for name in ['config/runtime/sdl-video.json','config/runtime/sdl-events.json','scripts/build-sdl-backend.py','scripts/test-sdl-video.py','scripts/test-sdl-error-oom.py','scripts/freeze-sdl-video.py','port/licenses/SDL3-LICENSE.txt','generated/tools/external-memory-pass/result.json','generated/integration-review/read-file-checkpoint-20261003/checkpoint.json','docs/STATUS.md','docs/EXECUTION-PLAN.md']:add(R/name)
(out/'checkpoint.json').write_text(json.dumps(dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_exit=d['exit'],continuation_result=d['result'],scope='Native Cocoa/UIKit video initialization with real guest ABI/allocator and Apple lifecycle passes component tests. Ordinary startup still stops at the worker file query. A separately qualified debugger run pauses that worker without returning its service, observes original main VIDEO success, then stops on a protected runtime code patch. No original game window/rendering/control/HD/authenticated multiplayer acceptance.'),indent=2)+'\n')
for name,h in files.items():assert sha(out/name)==h
print('PASS:',len(files),'source-verified files frozen')
