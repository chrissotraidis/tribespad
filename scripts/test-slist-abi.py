#!/usr/bin/env python3
"""Execute actual generated SLIST initialization wrapper on macOS or one booted simulator."""
from checked_support import compile_support
from guard_checked_link import guard_link
import argparse,hashlib,importlib.util,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));booted=[d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted'];assert booted==[a.simulator_device]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12')
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();out=R/'generated/native-win32/slist-abi'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator);bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(generator.generate({k:bindings[k] for k in ('InitializeSListHead','InterlockedFlushSList')}))
lookup=out/'lookup.ll';lookup.write_text('declare hidden fastcc void @tbimp_InitializeSListHead(ptr)\ndeclare hidden fastcc void @tbimp_InterlockedFlushSList(ptr)\ndeclare fastcc void @tribes_return_to_host(ptr)\ndeclare void @abort() noreturn\ndefine ptr @tribes_lookup(i32 %pc) {\n switch i32 %pc, label %bad [i32 1, label %init i32 2, label %flush i32 4294963200, label %host]\ninit: ret ptr @tbimp_InitializeSListHead\nflush: ret ptr @tbimp_InterlockedFlushSList\nhost: ret ptr @tribes_return_to_host\nbad: call void @abort()\n unreachable\n}\n')
sources=[R/'port/core/guest_slist.c',R/'port/core/guest_heap.c',R/'tests/slist_abi_test.c',R/'tests/fixtures/native_enter.ll',wrapper,lookup];runs=[]
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
objects,_=compile_support([p for p in sources if p.name!='slist_abi_test.c'],out/'checked',['-DPTROFS_64BIT'])
meta=json.loads((out/'checked/result.json').read_text());entries=[dict(object=str(p),object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']) for p in map(pathlib.Path,objects)];guarded,_=guard_link(entries,out/'guarded')
for mode in (['normal'] if a.simulator_device else ['normal','tsan','checked']):
 exe=out/(mode+"-test");cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module']
 if mode=='tsan':cmd+=['-fsanitize=thread']
 inputs=([R/'tests/slist_abi_test.c',*guarded] if mode=='checked' else sources)+guards
 if mode=='checked':cmd+=['-DCHECKED_TEST']
 compile_result=subprocess.run(cmd+list(map(str,inputs))+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(compile_result.stdout+compile_result.stderr);assert not compile_result.returncode,compile_result.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 if mode=='checked':
  fault=subprocess.run([str(exe),'unreadable'],capture_output=True,text=True,timeout=10);assert fault.returncode==-6 and 'checked guest memory violation' in fault.stderr,(fault.returncode,fault.stderr)
 runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
report={'state':'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'runs':runs,'sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'bindings_sha256':hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest(),'scope':'Actual ARM64 emitted stdcall service-wrapper execution; no QoL ARM64 module/game-session claim'};report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
