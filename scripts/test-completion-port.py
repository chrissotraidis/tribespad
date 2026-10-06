#!/usr/bin/env python3
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/completion-port';out.mkdir(exist_ok=True);(out/'result.json').write_text(json.dumps(dict(passed=False,state='running'))+'\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=R/'port/llasm-runtime/win32-bindings.json';wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({name:json.loads(bindings.read_text())[name] for name in ['CreateIoCompletionPort','PostQueuedCompletionStatus','GetQueuedCompletionStatus','GetQueuedCompletionStatusEx','CloseHandle']}))
sources=[R/name for name in ['port/core/guest_heap.c','port/core/native_handles.c','tests/fixtures/completion_observer.c','tests/fixtures/completion_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
objects,checked=compile_support(sources,out/'checked',['-DTRIBESPAD_COMPLETION_WAIT_OBSERVER']);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
objects,guarded=guard_link(entries,out/'guarded');test=R/'tests/completion_port_test.c';guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];exe=out/'test'
subprocess.run(['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-DCHECKED_TEST','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module',str(test),*map(str,guards),*objects,'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr);assert not run.returncode and run.stdout.startswith('PASS:'),(run.returncode,run.stdout,run.stderr)
report=dict(passed=True,checked_build_sha256=checked,guarded_build_sha256=guarded,binary_sha256=sha(exe),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[test,*guards,bindings,R/'port/core/native_handles.h',R/'port/llasm-support/llasm_cpu.h',pathlib.Path(__file__).resolve()]},scope='Owned single-consumer completion queues through five emitted ABIs and both compiler guards; scalar/batch interoperation,128-entry capacity and short batches,8,000 concurrent producer packets, timeout and close wakeup. File/socket association, multi-consumer scheduling, duplication and async operation production remain explicit gates.')
print(run.stdout,end='')

for mode in ['normal','tsan']:
 target=out/('test-'+mode)
 command=['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-DTRIBESPAD_COMPLETION_WAIT_OBSERVER','-Wno-override-module',str(test),*map(str,guards),*map(str,sources),'-o',str(target)]
 if mode=='tsan':command+=['-fsanitize=thread']
 subprocess.run(command,check=True)
 run=subprocess.run([str(target)],capture_output=True,text=True,timeout=60)
 (out/(mode+'-stdout.log')).write_text(run.stdout);(out/(mode+'-stderr.log')).write_text(run.stderr)
 assert not run.returncode and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
 if mode=='tsan':assert not run.stderr,run.stderr
 report[mode]=dict(passed=True,binary_sha256=sha(target))
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: completion queue normal and ThreadSanitizer runs')
