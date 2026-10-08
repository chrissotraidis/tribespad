#!/usr/bin/env python3
"""Exercise the reached music-decoder file IO group through original cdecl ABI."""
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
from sdl_backend_inputs import inputs
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-io';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text('{"passed":false,"state":"building"}\n')
backend,flags,libraries=inputs();spec=importlib.util.spec_from_file_location('gen',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=json.loads((R/'config/runtime/sdl-io-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate(bindings,False));lookup=out/'lookup.ll';names=list(bindings)
lookup.write_text('\n'.join('declare hidden fastcc void @tbimp_'+n+'(ptr)' for n in names)+'''
declare fastcc void @tribes_return_to_host(ptr)
declare void @abort() noreturn
define ptr @tribes_lookup(i32 %pc) {
 switch i32 %pc, label %bad [
'''+''.join('i32 %d, label %%case%d\n'%(i,i) for i in range(1,len(names)+1))+'''i32 4294963200, label %host]
'''+''.join('case%d: ret ptr @tbimp_%s\n'%(i,n) for i,n in enumerate(names,1))+'''host: ret ptr @tribes_return_to_host
bad: call void @abort()
 unreachable
}
''')
sources=[R/'port/core'/n for n in ['guest_sdl_io.c','guest_filesystem.c','guest_heap.c']]+[wrapper,lookup,R/'tests/fixtures/native_enter.ll']
objects,_=compile_support(sources,out/'checked',['-DPTROFS_64BIT',*flags]);meta=json.loads((out/'checked/result.json').read_text());entries=[dict(object=str(p),object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']) for p in map(pathlib.Path,objects)];guarded,_=guard_link(entries,out/'guarded')
fixture=R/'tests/sdl_io_test.c';guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];runs=[]
for mode in ['normal','checked','tsan']:
 exe=out/(mode+'-test');cmd=['clang','-target','arm64-apple-macos12','-isysroot',backend['sdk'],'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module',*flags]+(['-fsanitize=thread'] if mode=='tsan' else [])
 subprocess.run(cmd+list(map(str,[fixture,*guards,*(guarded if mode=='checked' else sources)]))+libraries+['-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/(mode+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(mode,run.returncode,run.stdout,run.stderr)
 faults=[]
 if mode=='checked':
  for fault in ['unreadable-path','stale','foreign','readonly-output','short-output','string-unreadable','string-readonly-output','string-short-output']:
   run=subprocess.run([str(exe),fault],capture_output=True,text=True,timeout=30);assert run.returncode==-6 and 'TribesPad:' in run.stderr,(fault,run.returncode,run.stderr);faults.append(fault)
 runs.append(dict(mode=mode,passed=True,faults=faults,binary_sha256=sha(exe)));print('PASS:',mode,flush=True)
paths=[*sources[:3],fixture,pathlib.Path(__file__).resolve(),R/'config/runtime/sdl-io-bindings.json',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json']
(out/'result.json').write_text(json.dumps(dict(passed=True,state='finished',runs=runs,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Real read-only file IO and emitted guest ABI; original decoder execution and full music playback unverified.'),indent=2)+'\n')
