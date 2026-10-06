#!/usr/bin/env python3
"""Freeze source-verified file metadata and the actual worker curl boundary."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/nt-file-checkpoint-20261003';assert not (out/'checkpoint.json').exists();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();files={}
def add(p):
 p=p.resolve();rel=p.relative_to(R);dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
 if str(rel) in files:return
 subprocess.run(['cp','-c',str(p),str(dst)],check=True);assert sha(p)==sha(dst);files[str(rel)]=sha(dst)
def sources(d,key):
 for name,h in d[key].items():assert sha(R/name)==h,name;add(R/name)
for rel in ['nt-file/macos','nt-file/B8138F85-0AE9-4494-9564-EDC7FD4E3B7F','nt-volume/macos','read-file/macos']:
 root=R/'generated/integration-review'/rel;d=json.loads((root/'result.json').read_text());assert d['passed'];add(root/'result.json');sources(d,'sources_sha256')
 for row in d['runs']:
  p=root/('test-'+row['mode']);assert sha(p)==row['binary_sha256'];add(p)
 for p in root.iterdir():
  if p.is_file() and p.suffix=='.log':add(p)
root=R/'generated/integration-review/file-metadata/macos';d=json.loads((root/'result.json').read_text());assert d['passed'];add(root/'result.json');sources(d,'sources_sha256');assert sha(root/'test')==d['binary_sha256'];add(root/'test');add(root/'run.log')
root=R/'generated/native-win32/current-directory-regressions';d=json.loads((root/'result.json').read_text());assert d['pass'];add(root/'result.json');assert sha(R/'scripts/test-filesystem-regressions.py')==d['test_driver_sha256'];add(R/'scripts/test-filesystem-regressions.py')
for name,row in d['tests'].items():
 sources(row,'sources_sha256')
 for run in row['runs']:
  p=root/(name+'-'+run['mode']);assert sha(p)==run['binary_sha256'];add(p);add(root/(name+'-'+run['mode']+'.log'))
for platform in ['macos','device','simulator']:
 root=R/'generated/native-win32'/platform;d=json.loads((root/'result.json').read_text());add(root/'result.json')
 for name in ['guest_file','guest_filesystem','native_handles']:
  p=R/'port/core'/(name+'.c');assert d['objects'][name]['source_sha256']==sha(p);add(p)
 assert d['bindings_sha256']==sha(R/'port/llasm-runtime/win32-bindings.json')
root=R/'generated/native-win32/checked-macos';d=json.loads((root/'result.json').read_text());assert d['state']=='finished' and not d['failures'];assert d['source_build_sha256']==sha(R/'generated/native-win32/macos/result.json');add(root/'result.json')
for name in ['guest_file','guest_filesystem','native_handles']:
 row=d['objects'][name];assert row['source_sha256']==sha(R/row['source']);assert row['object_sha256']==sha(root/(name+'.o'));add(root/(name+'.o'))
root=R/'generated/integration-review/system-providers';d=json.loads((root/'result.json').read_text());assert d['pass'] and d['runtime_build_sha256']==sha(R/'generated/native-win32/macos/result.json');add(root/'result.json');sources(d,'source_sha256');assert sha(root/'native')==d['binary_sha256'];add(root/'native');add(root/'run.log')
native=R/'generated/srw/run-20261003T150627-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';d=json.loads((native/'result.json').read_text());assert d['state']=='finished' and d['initialized'] and d['exit']==-6 and d['result']['reason']=='curl runtime ownership/lifecycle';assert sha(native/'bootstrap')==d['binary_sha256'];sources(d,'source_sha256');add(native/'result.json')
assert d['runtime_build_sha256']==sha(R/'generated/native-win32/macos/result.json') and d['checked_runtime_build_sha256']==sha(R/'generated/native-win32/checked-macos/result.json')
g=json.loads((native/'guarded/result.json').read_text());assert g['state']=='compiled' and g['config_sha256']==sha(R/'config/runtime/external-memory-contracts.json') and g['plugin_build_sha256']==sha(R/'generated/tools/external-memory-pass/result.json')
for name in ['bootstrap','engine.bin','qol.bin','stdout.log','stderr.log','support/result.json','guarded/result.json','guarded/policy.json']:add(native/name)
evidence=R/'generated/integration-review/nt-file';capture=json.loads((evidence/'query-execution.json').read_text());assert capture['binary_sha256']==d['binary_sha256'];assert capture['log_sha256']==sha(evidence/'query-lldb.log');assert capture['capture_script_sha256']==sha(evidence/'capture_queries.py') and capture['driver_sha256']==sha(evidence/'run_capture.py');assert capture['next_stop']['return_pc']=='0x100236ca'
for row in capture['returns']:assert row['native_result'] in [0,0x80000005]
assert len(capture['returns'])==3 and not capture['probes']
for p in evidence.rglob('*'):
 if p.is_file() and p.parent in [evidence,evidence/'first-capture-error'] and p.suffix in ['.json','.log','.py']:add(p)
for name in ['port/core/guest_file.h','port/core/native_handles.h','port/core/guest_filesystem.h','port/core/guest_curl_allocator.h','config/runtime/nt-file.json','config/runtime/nt-volume.json','port/llasm-runtime/win32-bindings.json','scripts/gen-native-system-providers.py','scripts/build-native-win32.py','scripts/build-checked-runtime.py','scripts/test-qol-native-bootstrap.py','scripts/freeze-nt-file.py','generated/tools/external-memory-pass/result.json','generated/integration-review/sdl-video-checkpoint-20261003/checkpoint.json','docs/STATUS.md','docs/EXECUTION-PLAN.md']:add(R/name)
(out/'checkpoint.json').write_text(json.dumps(dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_exit=d['exit'],continuation_result=d['result'],scope='Real file and virtual-volume metadata passes Mac and sole iPad fixtures. Original worker receives actual file/volume warnings and continues to curl_easy_init on worker7, where existing owner-thread guard stops. No original file-content read, window, map, HD, gameplay controls, physical-device or authenticated match acceptance.'),indent=2)+'\n')
for name,h in files.items():assert sha(out/name)==h
print('PASS:',len(files),'source-verified files frozen')
