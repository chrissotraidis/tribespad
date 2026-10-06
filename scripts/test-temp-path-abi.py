#!/usr/bin/env python3
"""Execute actual generated temporary-path and timing wrappers on macOS or one booted simulator."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));booted=[d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted'];assert booted==[a.simulator_device]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12')
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();out=R/'generated/native-win32/temp-path-abi'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator);bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(generator.generate({k:bindings[k] for k in ('GetTempPathW','GetTempPath2W','QueryPerformanceCounter','QueryPerformanceFrequency','GetWindowsDirectoryA','GetSystemDirectoryA','SHGetFolderPathA')}))
sources=[R/'port/core/process_environment.c',R/'port/core/performance_clock.c',R/'port/core/guest_filesystem.c',R/'port/core/guest_heap.c',R/'tests/temp_path_abi_test.c',R/'tests/fixtures/native_enter.ll',R/'tests/fixtures/temp_path_dispatch.c',wrapper];runs=[]
from checked_support import compile_support
from guard_checked_link import guard_link
support=[p for p in sources if p.name!='temp_path_abi_test.c'];objects,checked=compile_support(support,out/'checked',['-DPTROFS_64BIT'],target=target,sdk_name=sdk_name);meta=json.loads((out/'checked/result.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
entries=[dict(object=str(p),object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']) for p in map(pathlib.Path,objects)];objects,guarded=guard_link(entries,out/'guarded',target=target)

for mode in (['normal'] if a.simulator_device else ['normal','tsan','checked']):
 exe=out/(mode+'-probe');cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module']
 if mode=='tsan':cmd+=['-fsanitize=thread']
 selected=objects+[str(R/'tests/temp_path_abi_test.c'),str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c')] if mode=='checked' else list(map(str,sources))+[str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c')]
 if mode=='checked':cmd+=['-DCHECKED']
 compile_result=subprocess.run(cmd+selected+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(compile_result.stdout+compile_result.stderr);assert not compile_result.returncode,compile_result.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 faults=[]
 if mode=='checked' and not a.simulator_device:
  for case in ['unsupported-folder','foreign-token','readonly-folder','cross-page-folder']:
   v=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=30);(out/(case+'.log')).write_text(v.stdout+v.stderr);expected='checked guest memory violation' if case in ['readonly-folder','cross-page-folder'] else 'unsupported shell folder query';assert v.returncode==-6 and expected in v.stderr,(case,v.returncode,v.stderr);faults.append(case)
 runs.append({'mode':mode,'pass':True,'faults':faults,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
report={'state':'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'runs':runs,'sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'checked_sha256':checked,'guarded_sha256':guarded,'bindings_sha256':hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest(),'scope':'Actual ARM64 emitted stdcall service-wrapper execution; no QoL ARM64 module/game-session claim'};report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
