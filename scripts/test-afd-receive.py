#!/usr/bin/env python3
"""Original AFD UDP readiness over native descriptors and real completions."""
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/afd-receive';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();report=out/'result.json';report.write_text('{"passed":false,"state":"building"}\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
config=R/'port/llasm-runtime/win32-bindings.json';bindings=json.loads(config.read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({n:bindings[n] for n in ['WSAStartup','WSACleanup','socket','closesocket','bind','ioctlsocket','WSAIoctl','ConnectEx','setsockopt','CreateIoCompletionPort','SetFileCompletionNotificationModes','GetQueuedCompletionStatus','GetQueuedCompletionStatusEx','CloseHandle','WSARecv','WSASend','CancelIoEx','CancelIo','NtDeviceIoControlFile','WSARecvFrom','WSASendTo']},False))
sources=[R/n for n in ['port/core/native_winsock.c','port/core/native_handles.c','port/core/guest_heap.c','port/core/guest_encoding.c','tests/fixtures/afd_receive_dispatch.c','tests/fixtures/native_enter.ll','tests/fixtures/connect_observation.c']]+[wrapper]
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/afd_receive_test.c'
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();clang='clang';flags=['-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-DTB_SOCKET_CONNECT_OBSERVATION','-Wall','-Wextra','-Werror','-Wno-override-module'];runs=[]
for mode in ['normal','tsan','checked']:
 work=out/mode;work.mkdir(parents=True,exist_ok=True);objects=[];checked=guarded=None
 extra=['-fsanitize=thread'] if mode=='tsan' else ['-DCHECKED'] if mode=='checked' else []
 if mode=='checked':
  objects,checked=compile_support(sources,work/'checked',['-DPTROFS_64BIT','-DTB_SOCKET_CONNECT_OBSERVATION'])
  meta=json.loads((work/'checked/result.json').read_text());entries=[]
  for path in objects:
   p=pathlib.Path(path);row=meta['objects'][p.stem];entries.append(dict(object=path,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
  objects,guarded=guard_link(entries,work/'guarded')
 else:
  for i,p in enumerate(sources):
   obj=work/(str(i)+'.o');subprocess.run([clang,*flags,*extra,'-c',str(p),'-o',str(obj)],check=True);objects.append(str(obj))
 exe=work/'test';subprocess.run([clang,*flags,*extra,str(test),*map(str,guards),*objects,'-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=40);(work/'stdout.log').write_text(run.stdout);(work/'stderr.log').write_text(run.stderr)
 assert run.returncode==0 and run.stdout.startswith('PASS:') and 'ThreadSanitizer:' not in run.stderr,(mode,run.returncode,run.stdout,run.stderr)
 if mode=='checked':assert run.stderr.count('checked guest memory violation')==3 and run.stderr.count('guest pointer passed to host-only call')==4,run.stderr
 runs.append(dict(mode=mode,passed=True,binary_sha256=sha(exe),checked_sha256=checked,guarded_sha256=guarded,faults=7 if mode=='checked' else 0))
 print(mode,run.stdout.strip(),flush=True)
report.write_text(json.dumps(dict(passed=True,state='finished',runs=runs,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[test,*guards,config,R/'port/core/native_winsock.h',R/'port/core/guest_encoding.h',R/'port/core/native_handles.h',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]},scope='Original AFD zero-byte UDP readiness, packet retention and cancellation ownership through emitted NT ABI. No original-game packet transport or authenticated multiplayer acceptance.'),indent=2)+'\n')
