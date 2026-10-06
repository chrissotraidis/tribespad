#!/usr/bin/env python3
"""Freeze verified curl worker support and the preserved original startup failures."""
import datetime,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/curl-threads-checkpoint-20261003';assert not (out/'checkpoint.json').exists();files={};sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def add(p):
 p=p.resolve();rel=p.relative_to(R)
 if str(rel) in files:return
 dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True);subprocess.run(['cp','-c',str(p),str(dst)],check=True);assert sha(p)==sha(dst);files[str(rel)]=sha(dst)
def source_map(d,key='sources_sha256'):
 for n,h in d[key].items():assert sha(R/n)==h,n;add(R/n)
def binary(p,h):assert sha(p)==h,p;add(p)
for name in ['curl-threads/macos','curl-threads/B8138F85-0AE9-4494-9564-EDC7FD4E3B7F','sdl-video/macos','sdl-video/B8138F85-0AE9-4494-9564-EDC7FD4E3B7F','sdl-hints','curl-runtime','external-memory-pass']:
 root=R/'generated/integration-review'/name;d=json.loads((root/'result.json').read_text());assert d.get('passed',d.get('pass_'));add(root/'result.json');source_map(d)
 for row in d.get('runs',[]):
  if 'app' in row:
   app=R/row['app'];p=app/('SDLVideoProbe' if 'B813' in name else 'Contents/MacOS/SDLVideoProbe')
  else:p=root/('test-'+row['mode'])
  binary(p,row['binary_sha256'])
 if name=='curl-runtime':binary(root/'test',d['binary_sha256'])
 if name=='sdl-hints':
  for mode in ['normal','tsan','checked']:binary(root/('test-'+mode),d[mode]['binary_sha256'])
 for sub,key in [('checked','checked_sha256'),('guarded','guarded_sha256')]:
  if key in d:binary(root/sub/'result.json',d[key]);add(root/sub/'result.json')
 for p in root.iterdir():
  if p.is_file() and p.suffix in ['.log','.json','.crt','.cnf']:add(p)
root=R/'generated/integration-review/curl-threads/platforms';d=json.loads((root/'result.json').read_text());assert d['passed'] and not d['execution_tested'];add(root/'result.json')
for row in d['rows']:binary(R/row['source'],row['source_sha256']);binary(R/row['object'],row['object_sha256'])
native=R/'generated/srw/run-20261003T150627-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';d=json.loads((native/'result.json').read_text());assert d['state']=='finished' and d['initialized'] and d['exit']==-6 and d['result']['kind']=='memory_violation';assert '0x5bb96b' in d['result']['reason'];binary(native/'bootstrap',d['binary_sha256']);source_map(d,'source_sha256')
assert d['external_build_sha256']==sha(native/'guarded/result.json')
g=json.loads((native/'guarded/result.json').read_text());assert g['config_sha256']==sha(R/'config/runtime/external-memory-contracts.json') and g['plugin_build_sha256']==sha(R/'generated/tools/external-memory-pass/result.json')
for n in ['result.json','stdout.log','stderr.log','engine.bin','qol.bin','support/result.json','guarded/result.json','guarded/policy.json']:add(native/n)
evidence=R/'generated/integration-review/curl-threads';c=json.loads((evidence/'worker-execution.json').read_text());assert not c['suspended'];assert c['binary_sha256']==d['binary_sha256'];assert c['log_sha256']==sha(evidence/'worker-lldb.log');assert c['capture_script_sha256']==sha(evidence/'capture_worker.py') and c['driver_sha256']==sha(evidence/'run_capture.py');assert any(x['api']=='tribes_curl_easy_perform' and x['native_result']==0 for x in c['returns']);assert c['returns'][-1]['api']=='tribes_curl_easy_cleanup'
for root in [evidence,evidence/'first-perform-capture',R/'generated/integration-review/post-video']:
 for p in root.iterdir():
  if p.is_file() and p.suffix in ['.py','.log','.json','.bin']:add(p)
for n in ['config/runtime/curl-threads.json','scripts/test-curl-thread-platforms.py','scripts/freeze-curl-threads.py','docs/STATUS.md','docs/EXECUTION-PLAN.md','generated/tools/external-memory-pass/result.json','generated/integration-review/nt-file-checkpoint-20261003/checkpoint.json']:add(R/n)
report=dict(passed=True,goal_complete=False,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,continuation_result=d['result'],scope='Curl component threading/HTTPS passes Mac and sole iPad, original perform/cleanup observed without manual thread suspension. Ordinary full launch remains a rejected main code write; another worker schedule stops at byte LOCK CMPXCHG. No response body, original window, HD, gameplay or authenticated match acceptance.')
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for n,h in files.items():assert sha(out/n)==h
print('PASS:',len(files),'source-verified files frozen; goal remains active')
