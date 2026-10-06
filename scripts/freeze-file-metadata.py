#!/usr/bin/env python3
"""Freeze source-verified metadata-file tests and the original-client boundary."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/file-metadata-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();files={}
def add(p):
 p=p.resolve();rel=p.relative_to(R);dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if str(rel) in files:return
 subprocess.run(['cp','-c',str(p),str(dst)],check=True);assert sha(p)==sha(dst);files[str(rel)]=sha(dst)
for rel in ['file-security','file-metadata/B8138F85-0AE9-4494-9564-EDC7FD4E3B7F','write-file']:
 root=R/'generated/integration-review'/rel;report=json.loads((root/'result.json').read_text());assert report['passed'];add(root/'result.json')
 for name,h in report['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name)
 for p in root.iterdir():
  if p.is_file() and (p.suffix=='.log' or p.name in ['normal','tsan','checked','test']):add(p)
root=R/'generated/native-win32/current-directory-regressions';r=json.loads((root/'result.json').read_text());assert r['pass'];add(root/'result.json')
for row in r['tests'].values():
 for name,h in row['sources_sha256'].items():assert sha(R/name)==h,name;add(R/name)
for platform in ['macos','device','simulator']:
 p=R/'generated/native-win32'/platform/'result.json';r=json.loads(p.read_text());add(p)
 for name in ['guest_file','guest_filesystem','native_handles']:assert r['objects'][name]['source_sha256']==sha(R/'port/core'/(name+'.c'))
p=R/'generated/native-win32/checked-macos/result.json';add(p)
native=R/'generated/srw/run-20261003T150627-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';r=json.loads((native/'result.json').read_text());assert r['state']=='finished' and r['initialized'] and r['exit']==71 and r['result']['import']['name']=='SDL_SetLogOutputFunction';assert sha(native/'bootstrap')==r['binary_sha256']
assert 'unsupported CreateFile' not in r['stderr']
for name,h in r['source_sha256'].items():assert sha(R/name)==h,name;add(R/name)
for name in ['bootstrap','engine.bin','qol.bin','result.json','stdout.log','stderr.log','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name)
for name in ['port/core/guest_filesystem.h','port/core/native_handles.h','config/runtime/file-metadata.json','scripts/freeze-file-metadata.py','generated/integration-review/bcrypt-provider/file-open-analysis.json','generated/integration-review/bcrypt-vm-continuation-checkpoint-20261003/manifest.json','generated/tools/external-memory-pass/ExternalMemoryPass.dylib','generated/tools/external-memory-pass/result.json']:add(R/name)
(out/'checkpoint.json').write_text(json.dumps(dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_exit=r['exit'],continuation_result=r['result'],scope='Metadata-file semantics pass Mac normal/TSan/guarded ABI and sole guarded iPad. Original client reaches SDL logging with workers active and no file-mode abort in this run; worker completion is not established. No game window, map, controls, HD or authenticated match acceptance.'),indent=2)+'\n')
for name,h in files.items():assert sha(out/name)==h
print('PASS:',len(files),'source-verified files frozen')
