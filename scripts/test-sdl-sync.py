#!/usr/bin/env python3
import argparse,hashlib,json,pathlib,subprocess
from sdl_backend_inputs import inputs
from checked_support import compile_support
from guard_checked_link import guard_link
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();platform='simulator' if a.simulator_device else 'macos';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12'
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-sync'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();backend,flags,libraries=inputs(platform);source=R/'port/core/guest_sdl_sync.c';test=R/'tests/sdl_sync_test.c'
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [v['udid'] for rows in d['devices'].values() for v in rows if v['state']=='Booted']==[a.simulator_device]
objects,_=compile_support([source],out/'checked',['-DPTROFS_64BIT',*flags],target=target,sdk_name='iphonesimulator' if a.simulator_device else 'macosx');m=json.loads((out/'checked/result.json').read_text());entries=[]
for o in objects:
 p=pathlib.Path(o);entries.append(dict(object=o,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=m['objects'][p.stem]['bitcode_sha256']))
objects,_=guard_link(entries,out/'guarded',target=target);rows=[]
for mode in (['checked'] if a.simulator_device else ['normal','tsan','checked']):
 exe=out/("test-"+mode);args=objects+[str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c')] if mode=='checked' else [str(source)]
 subprocess.run(['clang','-target',target,'-isysroot',backend['sdk'],'-O1','-g','-Wall','-Wextra','-Werror',*flags,*(['-DTRIBESPAD_SIMULATOR'] if a.simulator_device else []),str(test),*args,*libraries,*(['-fsanitize=thread'] if mode=='tsan' else []),'-o',str(exe)],check=True)
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=30);(out/(mode+'.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and run.stderr.count('invalid SDL synchronization ownership/lifetime')==(0 if a.simulator_device else 8) and 'ThreadSanitizer:' not in run.stderr,(mode,run.returncode,run.stdout,run.stderr);rows.append(dict(mode=mode,binary_sha256=sha(exe)));print(mode,run.stdout.strip(),flush=True)
(out/'result.json').write_text(json.dumps(dict(passed=True,simulator_device=a.simulator_device,runs=rows,sources_sha256={str(p.relative_to(R)):sha(p) for p in [source,test,R/'port/core/guest_sdl_sync.h',R/'config/runtime/sdl-bindings.json',R/'config/runtime/external-memory-contracts.json',R/'port/compiler/ExternalMemoryPass.cpp',pathlib.Path(__file__).resolve()]},scope='Real native SDL mutex/condition semantics across guest threads; typed tokens and lifecycle guards. Generic emitted cdecl wrappers are integrated separately; no audio playback or whole-engine claim.'),indent=2)+'\n')
