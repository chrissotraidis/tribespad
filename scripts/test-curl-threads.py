#!/usr/bin/env python3
"""Guest ABI curl thread handoff, allocator identity and callback reentry."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess,http.server,ssl,threading,email.utils,datetime
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();platform='simulator' if a.simulator_device else 'macos'
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [x['udid'] for rows in d['devices'].values() for x in rows if x['state']=='Booted']==[a.simulator_device]
target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdkname='iphonesimulator' if a.simulator_device else 'macosx';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip()
out=R/'generated/integration-review/curl-threads'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
prefix=R/'generated/http-backend'/platform/'install';backend=json.loads((prefix.parent/'result.json').read_text())
for name,h in backend['libraries'].items():assert sha(R/name)==h
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'config/runtime/curl-bindings.json';b=json.loads(bindings.read_text())
for name,helper,count,result in [('ThreadMalloc','curl_threads_malloc',1,True),('ThreadFree','curl_threads_free',1,False),('ThreadRealloc','curl_threads_realloc',2,True),('ThreadStrdup','curl_threads_strdup',1,True),('ThreadCalloc','curl_threads_calloc',2,True),('ThreadTimer','curl_threads_timer',3,True),('ThreadWrite','curl_threads_write',4,True),('curl_multi_cleanup','tribes_curl_multi_cleanup',1,True),('curl_global_cleanup','tribes_curl_global_cleanup',0,False)]:
 b[name]=dict(helper=helper,arguments=count,returns=result,helper_returns='i32' if result else 'void',guest_base=False,cpu_argument=True,calling_convention='cdecl')
wrapper=out/'bindings.ll';wrapper.write_text(gen.generate(b,False))
sources=[R/n for n in ['port/core/guest_curl.c','port/core/guest_curl_allocator.c','port/core/guest_callback.c','port/core/guest_heap.c','port/core/guest_filesystem.c','port/core/native_winsock.c','port/core/guest_encoding.c','tests/fixtures/curl_thread_dispatch.c','tests/fixtures/native_enter.ll','tests/fixtures/curl_thread_external.ll']]+[wrapper]
flags=['-DPTROFS_64BIT','-I',str(prefix/'include')];objects,checked=compile_support(sources,out/'checked',flags,target=target,sdk_name=sdkname)
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target)
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/curl_threads_test.c';runs=[]
# Short-lived test CA and localhost server, never an application trust root.
cert=out/'ca.crt';key=out/'localhost.key';cnf=out/'certificate.cnf'
cnf.write_text('[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n[dn]\nCN=localhost\n[ext]\nsubjectAltName=DNS:localhost\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,digitalSignature,keyEncipherment,keyCertSign\n')
with (out/'certificate.log').open('w') as log:subprocess.run(['/usr/bin/openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-config',str(cnf),'-keyout',str(key),'-out',str(cert)],stdout=log,stderr=log,check=True)
key.chmod(0o600);context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(str(cert),str(key));requests=[]
class Handler(http.server.BaseHTTPRequestHandler):
 def handle(self):
  try:super().handle()
  except (BrokenPipeError,ConnectionResetError,ssl.SSLError):pass # Deliberate write/TLS rejection cases.
 def do_GET(self):
  requests.append(dict(path=self.path,agent=self.headers.get('User-Agent'),modified=self.headers.get('If-Modified-Since')))
  self.send_response(304 if self.path=='/conditional' else 200)
  body=b'' if self.path=='/conditional' else bytes((i*37+11)&255 for i in range(40000))
  self.send_header('Content-Length',str(len(body)));self.end_headers()
  try:self.wfile.write(body)
  except (BrokenPipeError,ConnectionResetError,ssl.SSLError):pass
 def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);server.socket=context.wrap_socket(server.socket,server_side=True)
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();url='https://localhost:'+str(server.server_port)
for mode in (['checked'] if a.simulator_device else ['normal','tsan','checked']):
 exe=out/('test-'+mode);cmd=['clang','-target',target,'-isysroot',sdk,'-O1','-g','-pthread','-Wno-override-module','-Wall','-Wextra','-Werror',*flags,str(test),*map(str,guards)]
 cmd+=['-DCHECKED',*objects] if mode=='checked' else list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 cmd+=[*[str(prefix/'lib'/n) for n in ['libcurl.a','libssl.a','libcrypto.a']],'-lz']
 if not a.simulator_device:cmd+=['-framework','SystemConfiguration','-framework','CoreFoundation','-framework','CoreServices']
 subprocess.run(cmd+['-o',str(exe)],check=True)
 command=['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)]
 r=subprocess.run(command+[str(out),url],capture_output=True,text=True,timeout=120);(out/(mode+'.log')).write_text(r.stdout+r.stderr);assert not r.returncode and r.stdout.startswith('PASS:') and not r.stderr,(r.returncode,r.stdout,r.stderr)
 faults=[]
 if mode=='checked':
  for case in ['cleanup-live','wrong-base','unframed','fixed-owner','write-callback','write-data','time-width','ca-output']:
   f=subprocess.run(command+[case],capture_output=True,text=True,timeout=20);(out/(mode+'-'+case+'.log')).write_text(f.stdout+f.stderr);assert f.returncode==(134 if a.simulator_device else -6) and ('curl runtime ownership/lifecycle' in f.stderr if case in ['cleanup-live','wrong-base'] else 'curl allocator ownership/frame' in f.stderr if case in ['unframed','fixed-owner'] else 'external memory contract stop' in f.stderr),(case,f.returncode,f.stderr);faults.append(case)
 runs.append(dict(mode=mode,passed=True,stdout=r.stdout,faults=faults,binary_sha256=sha(exe)));print(mode,r.stdout.strip())
server.shutdown();server.server_close();thread.join();key.unlink()
expected=email.utils.format_datetime(datetime.datetime.fromtimestamp(0xd202ef00,datetime.timezone.utc),usegmt=True)
assert requests and all(x['agent']=='TribesPad-runtime-test' and x['modified']==expected for x in requests),requests
assert len(requests)==12*len(runs)
(out/'https-requests.json').write_text(json.dumps(requests,indent=2)+'\n')
paths=sources+guards+[test,R/'port/core/guest_curl.h',R/'port/core/guest_curl_allocator.h',R/'port/core/native_winsock.h',R/'port/core/guest_filesystem.h',R/'config/runtime/external-memory-contracts.json',R/'port/compiler/ExternalMemoryPass.cpp',bindings,pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,state='finished',runs=runs,simulator_device=a.simulator_device,backend_sha256=sha(prefix.parent/'result.json'),https_requests=requests,certificate_sha256=sha(cert),checked_sha256=checked,guarded_sha256=guarded,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Serialized native curl operations across actual pthread guest ABI fixtures, allocator CPU/frame ownership and nested native timer callback. TSan instruments bridge/fixtures, not pinned native curl/LibreSSL libraries. No blocking-transfer latency, original game callback or authenticated session acceptance.'),indent=2)+'\n')
