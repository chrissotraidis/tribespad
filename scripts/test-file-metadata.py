#!/usr/bin/env python3
"""Run file-metadata ABI cases with both guards on Mac or the sole simulator."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [x['udid'] for rows in d['devices'].values() for x in rows if x['state']=='Booted']==[a.simulator_device]
target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdkname='iphonesimulator' if a.simulator_device else 'macosx';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip()
out=R/'generated/integration-review/file-metadata'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({n:json.loads(bindings.read_text())[n] for n in ['CreateFileA','CreateFileW']}))
sources=[R/n for n in ['port/core/guest_file.c','port/core/guest_filesystem.c','port/core/native_handles.c','port/core/guest_heap.c','tests/fixtures/file_security_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
objects,checked=compile_support(sources,out/'checked',[],target=target,sdk_name=sdkname)
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target)
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/file_metadata_test.c';exe=out/'test'
subprocess.run(['clang','-target',target,'-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-pthread','-Wno-override-module','-Wall','-Wextra','-Werror',str(test),*map(str,guards),*objects,'-o',str(exe)],check=True)
command=['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)]
r=subprocess.run(command,capture_output=True,text=True,timeout=60);(out/'run.log').write_text(r.stdout+r.stderr);assert not r.returncode and r.stdout.startswith('PASS:') and not r.stderr,(r.returncode,r.stdout,r.stderr)
paths=sources+guards+[test,R/'tests/fixtures/file_metadata_cases.h',R/'port/core/native_handles.h',R/'port/core/guest_filesystem.h',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,state='finished',stdout=r.stdout,simulator_device=a.simulator_device,binary_sha256=sha(exe),checked_sha256=checked,guarded_sha256=guarded,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Checked A/W component semantics, not game startup or physical-device acceptance; host file-security suite separately verifies inheritance and fault subprocesses.'),indent=2)+'\n');print(r.stdout,end='')
