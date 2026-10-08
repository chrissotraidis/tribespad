#!/usr/bin/env python3
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/network-startup-checked';out.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=R/'port/llasm-runtime/win32-bindings.json';wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({name:json.loads(bindings.read_text())[name] for name in ['WSAStartup','WSACleanup','socket','closesocket','bind','getsockname','getsockopt','SetHandleInformation','setsockopt']},False))
sources=[R/name for name in ['port/core/native_winsock.c','port/core/guest_encoding.c','port/core/guest_heap.c','tests/fixtures/network_host_calls.c','tests/fixtures/network_bad_abi.ll','tests/fixtures/network_startup_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
objects,checked=compile_support(sources,out/'checked',[]);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded');test=R/'tests/network_startup_checked_test.c';guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];exe=out/'test'
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
subprocess.run(['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module',str(test),*map(str,guards),*objects,'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and run.stderr.count('external memory contract stop:')==11,(run.returncode,run.stdout,run.stderr)
report=dict(passed=True,checked_build_sha256=checked,guarded_build_sha256=guarded,binary_sha256=sha(exe),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[test,*guards,bindings,R/'port/core/native_winsock.h',R/'port/llasm-support/llasm_cpu.h',pathlib.Path(__file__).resolve()]},scope='Actual Winsock startup, socket creation/bind/name/close through both compiler guards and emitted ABI. Host kernel port ownership and release verified. Five guest-pointer and six mismatched-ABI calls stop before the kernel; no payload-transfer/overlapped/authentication claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout,end='')
