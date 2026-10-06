#!/usr/bin/env python3
"""Execute actual generated collation wrappers on macOS or one booted simulator."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));booted=[d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted'];assert booted==[a.simulator_device]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12')
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();out=R/'generated/native-win32/collation-abi'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator);bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(generator.generate({k:bindings[k] for k in ('CompareStringEx',)}))
subprocess.run(['python3',str(R/'scripts/gen-nls-collation.py'),'--check'],check=True,capture_output=True)
sources=[R/'port/core/guest_collation.c',R/'port/core/guest_heap.c',R/'tests/collation_abi_test.c',R/'tests/fixtures/native_enter.ll',wrapper];runs=[]
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module']
 if mode=='tsan':cmd+=['-fsanitize=thread']
 compile_result=subprocess.run(cmd+list(map(str,sources))+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(compile_result.stdout+compile_result.stderr);assert not compile_result.returncode,compile_result.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
faults={}
for name in ['locale','version','reserved','handle','large']:
 exe=out/'normal';run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe),name] if a.simulator_device else [str(exe),name],capture_output=True,text=True,timeout=10);(out/(name+'-fault.log')).write_text(run.stdout+run.stderr);assert run.returncode==(134 if a.simulator_device else -6) and 'collation unsupported' in run.stderr,(name,run.returncode,run.stderr);faults[name]={'returncode':run.returncode,'state':'explicit unsupported abort'}
report={'faults':faults,'generator_sha256':hashlib.sha256((R/'scripts/gen-win32-bindings.py').read_bytes()).hexdigest(),'state' :'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'runs':runs,'sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'headers_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [R/'port/core/guest_collation.h',R/'port/core/guest_heap.h',R/'port/core/nls_collation.inc',R/'tests/fixtures/collation_cases.inc',R/'config/encoding-sources.json']},'bindings_sha256':hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest(),'scope':'Actual ARM64 emitted stdcall service-wrapper execution; no QoL ARM64 module/game-session claim'};report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
