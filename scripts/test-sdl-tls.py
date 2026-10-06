#!/usr/bin/env python3
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from sdl_backend_inputs import inputs
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();platform='simulator' if a.simulator_device else 'macos';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';backend,flags,libraries=inputs(platform);out=R/'generated/integration-review/sdl-tls'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [v['udid'] for rows in d['devices'].values() for v in rows if v['state']=='Booted']==[a.simulator_device]
(out/'result.json').write_text('{"passed":false,"state":"building"}\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);all_bindings=json.loads((R/'config/runtime/sdl-bindings.json').read_text());bindings={n:all_bindings[n] for n in ['SDL_SetTLS','SDL_GetTLS','SDL_CleanupTLS']};bindings['TestDestructor']=dict(helper='sdl_tls_destructor',arguments=1,returns=False,guest_base=False,cpu_argument=True,calling_convention='cdecl');wrapper=out/'bindings.ll';wrapper.write_text(gen.generate(bindings,False))
sources=[R/n for n in ['port/core/guest_sdl_tls.c','port/core/guest_callback.c','port/core/guest_heap.c','tests/fixtures/sdl_tls_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper];objects,_=compile_support(sources,out/'checked',['-DPTROFS_64BIT',*flags],target=target,sdk_name='iphonesimulator' if a.simulator_device else 'macosx');meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,_=guard_link(entries,out/'guarded',target=target);guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/sdl_tls_test.c';rows=[]
for mode in (['checked'] if a.simulator_device else ['normal','tsan','checked']):
 exe=out/('test-'+mode);cmd=['clang','-target',target,'-isysroot',backend['sdk'],'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*flags,*map(str,guards),str(test)];cmd+=['-DCHECKED',*objects] if mode=='checked' else list(map(str,sources));cmd+=['-fsanitize=thread'] if mode=='tsan' else [];subprocess.run(cmd+libraries+['-o',str(exe)],check=True)
 command=['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)]
 run=subprocess.run(command,capture_output=True,text=True,timeout=30);(out/(mode+'.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and 'ThreadSanitizer:' not in run.stderr,(mode,run.returncode,run.stdout,run.stderr)
 if mode=='checked':
  for i in range(1,5):
   f=subprocess.run(command+[str(i)],capture_output=True,text=True,timeout=30);(out/('fault-%d.log'%i)).write_text(f.stdout+f.stderr);assert f.returncode and 'invalid SDL TLS ownership/lifecycle' in f.stderr,(i,f.returncode,f.stderr)
 rows.append(dict(mode=mode,binary_sha256=sha(exe)));print(mode,run.stdout.strip(),flush=True)
paths=sources+guards+[test,R/'port/core/guest_sdl_tls.h',R/'port/core/guest_callback.h',R/'config/runtime/sdl-bindings.json',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=rows,simulator_device=a.simulator_device,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Real native SDL storage and guest cdecl destructor ABI. Guest thread-finalizer hook is exercised here with live owner CPU; actual Win32 guest thread teardown is tested separately. No original mixer or playback acceptance.'),indent=2)+'\n')
