#!/usr/bin/env python3
"""Check real curl initialization through the 32-bit guest ABI and both guards."""
import argparse,hashlib,http.server,socket,importlib.util,json,os,pathlib,subprocess,threading
from checked_support import compile_support
from guard_checked_link import guard_link
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--simulator',metavar='BOOTED_UDID',help='Execute the component in the sole booted iOS Simulator')
args=parser.parse_args()
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/curl-runtime'
if args.simulator: out=out/'simulator'
out.mkdir(parents=True,exist_ok=True)
if (out/'result.json').exists():(out/'result.json').unlink()
platform='simulator' if args.simulator else 'macos'
target='arm64-apple-ios16-simulator' if args.simulator else 'arm64-apple-macos12'
sdk_name='iphonesimulator' if args.simulator else 'macosx'
device=None
if args.simulator:
 devices=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','-j'],text=True))
 booted=[d for group in devices['devices'].values() for d in group if d['state']=='Booted']
 assert len(booted)==1 and booted[0]['udid']==args.simulator,'Requires exactly the requested booted simulator; never starts a second simulator.'
 device=booted[0]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
prefix=R/'generated/http-backend'/platform/'install';backend=json.loads((prefix.parent/'result.json').read_text())
for name,h in backend['libraries'].items():assert sha(R/name)==h
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=json.loads((R/'config/runtime/curl-bindings.json').read_text())
for name,helper,n,returns in [('CurlFixtureMalloc','curl_fixture_malloc',1,True),('CurlFixtureFree','curl_fixture_free',1,False),('CurlFixtureRealloc','curl_fixture_realloc',2,True),('CurlFixtureStrdup','curl_fixture_strdup',1,True),('CurlFixtureCalloc','curl_fixture_calloc',2,True),('curl_multi_cleanup','tribes_curl_multi_cleanup',1,True),('curl_global_cleanup','tribes_curl_global_cleanup',0,False)]:
 bindings[name]=dict(helper=helper,arguments=n,returns=returns,helper_returns='i32' if returns else 'void',guest_base=False,cpu_argument=True,calling_convention='cdecl')
for name,helper,n,returns in [('curl_multi_setopt','tribes_curl_multi_setopt',3,True),('curl_easy_init','tribes_curl_easy_init',0,True),('curl_easy_cleanup','tribes_curl_easy_cleanup',1,False),('curl_multi_add_handle','tribes_curl_multi_add_handle',2,True),('curl_multi_remove_handle','tribes_curl_multi_remove_handle',2,True),('CurlFixtureTimer','curl_fixture_timer',3,True),('CurlFixtureTimerAlternate','curl_fixture_timer_alternate',3,True),('CurlFixtureHeader','curl_fixture_header',4,True),('CurlFixtureWrite','curl_fixture_write',4,True),('CurlFixtureProgress','curl_fixture_progress',9,True)]:
 bindings[name]=dict(helper=helper,arguments=n,returns=returns,helper_returns='i32' if returns else 'void',guest_base=False,cpu_argument=True,calling_convention='cdecl')
for name,helper,n in [('curl_multi_assign','tribes_curl_multi_assign',3),('curl_easy_getinfo','tribes_curl_easy_getinfo',3),('CurlFixtureSocket','curl_fixture_socket',5),('curl_multi_socket_action','tribes_curl_multi_socket_action',4),('curl_easy_setopt','tribes_curl_easy_setopt',3)]:
 bindings[name]=dict(helper=helper,arguments=n,returns=True,guest_base=False,cpu_argument=True,calling_convention='cdecl')
bindings['select']=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text())['select']
bindings['WSAIoctl']=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text())['WSAIoctl']
wrapper=out/'bindings.ll';wrapper.write_text(gen.generate(bindings,False))
sources=[R/n for n in ['port/core/guest_curl.c','port/core/guest_curl_allocator.c','port/core/guest_callback.c','port/core/guest_heap.c','port/core/guest_filesystem.c','port/core/native_winsock.c','port/core/guest_encoding.c','tests/fixtures/curl_allocator_driver.c','tests/fixtures/native_enter.ll','tests/fixtures/curl_external_wrong.ll','tests/fixtures/curl_external_pointer.ll','tests/fixtures/curl_timer_external.ll']]+[wrapper]
objects,checked=compile_support(sources,out/'checked',['-DPTROFS_64BIT','-DCHECKED_TEST','-DCURL_RUNTIME_TEST','-I',str(prefix/'include')],target=target,sdk_name=sdk_name);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for name in objects:
 p=pathlib.Path(name);entries.append(dict(object=name,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target)
test=R/'tests/curl_runtime_test.c';guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];exe=out/'test'
command=['clang','-target',target,'-isysroot',backend['sdk'],'-O1','-g','-DPTROFS_64BIT','-Wall','-Wextra','-Werror',str(test),*objects,*map(str,guards),*[str(prefix/'lib'/n) for n in ['libcurl.a','libssl.a','libcrypto.a']],'-lz']
if not args.simulator: command+=['-framework','SystemConfiguration','-framework','CoreFoundation','-framework','CoreServices']
command+=['-o',str(exe)]
subprocess.run(command,check=True)
version=subprocess.check_output(['vtool','-show-build',str(exe)],text=True)
assert ('platform IOSSIMULATOR\n' if args.simulator else 'platform MACOS\n') in version
(out/'build-version.txt').write_text(version)
run_command=['xcrun','simctl','spawn',args.simulator,str(exe)] if args.simulator else [str(exe)]
requests=[];ipv6_requests=[]
class Handler(http.server.BaseHTTPRequestHandler):
 protocol_version='HTTP/1.1'
 def do_GET(self):
  (ipv6_requests if self.server.address_family==socket.AF_INET6 else requests).append(dict(path=self.path,client_port=self.client_address[1],headers=dict(self.headers)))
  if self.path=='/redirect':
   self.send_response(302);self.send_header('Location','/short');self.send_header('Content-Length','0');self.end_headers()
  elif self.path=='/short':
   self.send_response(200);self.send_header('Content-Length','4294967301');self.send_header('Connection','close');self.end_headers();self.wfile.write(b'abc');self.wfile.flush();self.close_connection=True
  else:
   self.send_response(204);self.send_header('Content-Length','0');self.end_headers()
 def log_message(self,*unused):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
class IPv6Server(http.server.ThreadingHTTPServer):address_family=socket.AF_INET6
server6=IPv6Server(('::1',0),Handler);thread6=threading.Thread(target=server6.serve_forever,daemon=True);thread6.start()
environment=dict(os.environ);url='http://127.0.0.1:'+str(server.server_port)
environment[('SIMCTL_CHILD_' if args.simulator else '')+'TRIBES_CURL_SOCKET_TEST_URL']=url
environment[('SIMCTL_CHILD_' if args.simulator else '')+'TRIBES_CURL_IPV6_TEST_URL']='http://localhost:'+str(server6.server_port)+'/ipv6'
try:run=subprocess.run(run_command,env=environment,capture_output=True,text=True,timeout=45)
finally:server.shutdown();server.server_close();thread.join();server6.shutdown();server6.server_close();thread6.join()
(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:'),(run.returncode,run.stderr)
assert [x['path'] for x in ipv6_requests]==['/ipv6'],ipv6_requests
assert [x['path'] for x in requests]==['/one?check%3Done+two','/two?check%3Done+two','/redirect','/short','/cancel','/reset'],requests
assert requests[0]['client_port']==requests[1]['client_port'],requests
assert requests[0]['headers']['Accept']=='text/plain' and 'Accept' not in requests[1]['headers'],requests
assert [x['headers']['X-TribesPad'] for x in requests[:2]]==['one','two'] and all(x['headers']['X-Empty']=='' for x in requests[:2]),requests
(out/'http-requests.json').write_text(json.dumps(requests,indent=2)+'\n')
faults={}
for name in ['wrong-reset','reset-stale','reset-attached','completion-readonly','completion-cross-page','select-readonly','select-cross-page','base-readonly','base-count-readonly','base-cross-page','base-async','error-readonly','error-cross-page','error-revoked','error-overlap','url-unreadable','url-live-cleanup','url-active-free','url-stale','url-stale-transfer','list-unreadable','list-mutated','list-live-cleanup','list-active-free','list-stale','list-stale-transfer','wrong-init','wrong-multi','wrong-cleanup','wrong-global-cleanup','bad-callback','guest-callback','guest-multi','before-init','timer-bad-callback','timer-guest-callback','timer-guest-data','wrong-timer-width','wrong-timer-dynamic','wrong-timer-option','stale-easy','live-easy-cleanup','unsupported-option','borrowed-close','borrowed-cleanup','borrowed-owner','socket-bad-callback','close-bad-callback','easy-guest-string','wrong-easy-long','wrong-info-selector','wrong-action-width','wrong-version','version-protected']:
 if args.simulator: continue # Fault subprocesses are covered by the macOS run.
 run=subprocess.run([str(exe),name],capture_output=True,text=True,timeout=15);(out/(name+'.log')).write_text(run.stderr);assert run.returncode==-6,(name,run.returncode,run.stderr)
 expected='checked guest memory violation' if name in ['completion-readonly','completion-cross-page','version-protected','list-unreadable','url-unreadable','error-readonly','error-cross-page','error-revoked','base-readonly','base-count-readonly','base-cross-page','select-readonly','select-cross-page'] else 'unsupported WSAIoctl contract' if name=='base-async' else 'invalid curl allocator ownership/frame' if name=='list-mutated' else 'unsupported curl runtime ownership' if name.startswith(('list-','url-','error-')) else 'unsupported borrowed socket ownership' if name.startswith('borrowed-') else 'unverified call: curl_' if name.startswith('wrong-') else 'unsupported curl runtime ownership' if name in ['before-init','stale-easy','live-easy-cleanup','reset-stale','reset-attached'] else 'unsupported curl multi option' if name=='unsupported-option' else 'external memory contract stop'
 assert expected in run.stderr,(name,run.stderr);faults[name]=run.stderr.strip()
report=dict(passed=True,platform=platform,device=device,command=command,run_command=run_command,binary_sha256=sha(exe),checked_sha256=checked,guarded_sha256=guarded,faults=faults,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[test,*guards,pathlib.Path(__file__).resolve(),R/'scripts/checked_support.py',R/'scripts/guard_checked_link.py',R/'port/core/guest_curl.h',R/'port/core/native_winsock.h',R/'port/core/guest_curl_allocator.h',R/'config/runtime/curl-bindings.json',R/'port/llasm-runtime/win32-bindings.json',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json']},backend_sha256=sha(prefix.parent/'result.json'),http_requests=requests,ipv6_requests=ipv6_requests,scope='Stable library-lifetime version string before init, across cleanup and in callbacks matches the real backend; repeated calls restore native contents. Real socket/timer events through emitted cdecl guest fixtures, nested PRIVATE/assign calls, two localhost HTTP 204 responses sharing a connection and guest socket identity across reset of the same easy handle; reset clears PRIVATE, URL, callbacks and error-buffer ownership, socket-callback abort, borrowed IPv4/IPv6 TCP metadata/address conversion and lifetime checks. Real redirect, header/body/progress callback dispatch, nested getinfo, 64-bit advertised length4294967301, truncated-body error, header/progress cancellation and retained error-buffer synchronization/permissions are verified. Emitted stdcall WSAIoctl returns actual base socket identity with short/null/stale/permission checks. Emitted select verifies timeout, data/write readiness, EOF, failed-connect exceptfds with SO_ERROR preserved and checked set outputs. Real completion records preserve guest easy identity, success/malformed URL status, two-message queue counts, repeated reads, removal lifetime, and recursive callback NULL behavior. IPRESOLVE uses native long values and native invalid-value errors, with actual IPv4 and IPv6 HTTP204 transfers. Only reviewed curl options/info are implemented. Original engine callback execution, guest TLS and online gameplay are not established. Simulator run is a command-line component; fault subprocesses run only on macOS.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: guest curl socket/timer/HTTP lifecycle and '+str(len(faults))+' intended external/lifecycle faults')
