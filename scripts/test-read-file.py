#!/usr/bin/env python3
"""Execute checked file reads through emitted ABI on Mac or sole simulator."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [x['udid'] for rows in d['devices'].values() for x in rows if x['state']=='Booted']==[a.simulator_device]
target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdkname='iphonesimulator' if a.simulator_device else 'macosx';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip()
out=R/'generated/integration-review/read-file'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({n:json.loads(bindings.read_text())[n] for n in ['ReadFile']}))
sources=[R/n for n in ['port/core/guest_file.c','port/core/guest_filesystem.c','port/core/native_handles.c','port/core/guest_heap.c','tests/fixtures/read_file_dispatch.c','tests/fixtures/native_enter.ll','tests/fixtures/read_file_external.ll']]+[wrapper]
objects,checked=compile_support(sources,out/'checked',[],target=target,sdk_name=sdkname)
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target)
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/read_file_test.c';runs=[]
for mode in (['checked'] if a.simulator_device else ['normal','tsan','checked']):
 exe=out/('test-'+mode);cmd=['clang','-target',target,'-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-include','TargetConditionals.h','-pthread','-Wno-override-module','-Wall','-Wextra','-Werror',str(test),*map(str,guards)]
 cmd+=['-DCHECKED',*objects] if mode=='checked' else list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True)
 command=['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)]
 r=subprocess.run(command,capture_output=True,text=True,timeout=60);(out/(mode+'.log')).write_text(r.stdout+r.stderr);assert not r.returncode and r.stdout.startswith('PASS:') and not r.stderr,(r.returncode,r.stdout,r.stderr)
 faults=[]
 if mode=='checked':
  for case in ['output','buffer','cross','host-output','host-abi','overlapped']:
   f=subprocess.run(command+[case],capture_output=True,text=True,timeout=30);(out/(mode+'-'+case+'.log')).write_text(f.stdout+f.stderr);assert f.returncode==(134 if a.simulator_device else -6) and ('checked guest memory violation' in f.stderr if case in ['output','buffer','cross'] else 'read' in f.stderr if case.startswith('host-') else 'overlapped ReadFile is not implemented' in f.stderr),(case,f.returncode,f.stderr);faults.append(case)
 runs.append(dict(mode=mode,passed=True,stdout=r.stdout,faults=faults,binary_sha256=sha(exe)));print(mode,r.stdout.strip())
paths=sources+guards+[test,R/'port/core/guest_file.h',R/'port/core/native_handles.h',R/'port/core/guest_filesystem.h',R/'config/runtime/external-memory-contracts.json',bindings,pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,state='finished',runs=runs,simulator_device=a.simulator_device,checked_sha256=checked,guarded_sha256=guarded,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Synchronous regular-file ReadFile through bounded host-owned read buffers and checked guest writes. No async/device/pipe or game acceptance.'),indent=2)+'\n')
