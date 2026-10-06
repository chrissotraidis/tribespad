#!/usr/bin/env python3
"""Execute actual generated encoding wrappers on macOS or one booted simulator."""
from checked_support import compile_support
from guard_checked_link import guard_link
import argparse,hashlib,importlib.util,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));booted=[d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted'];assert booted==[a.simulator_device]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12')
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();out=R/'generated/native-win32/encoding-abi'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator);bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(generator.generate({k:bindings[k] for k in ('GetACP','GetOEMCP','GetCPInfo','IsValidCodePage','MultiByteToWideChar','GetStringTypeW','LCMapStringEx','WideCharToMultiByte','LCMapStringW','LCMapStringA','GetStringTypeA')}))
subprocess.run(['python3',str(R/'scripts/gen-codepage-table.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-classification.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-casing.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-composition.py'),'--check'],check=True)
sources=[R/'port/core/guest_encoding.c',R/'port/core/guest_heap.c',R/'tests/fixtures/encoding_dispatch.c',R/'tests/fixtures/native_enter.ll',wrapper];runs=[]
test=R/'tests/encoding_abi_test.c';guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
objects,checked=compile_support(sources,out/'checked',['-DPTROFS_64BIT'],target=target,sdk_name=sdk_name)
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target)
for mode in (['normal','checked'] if a.simulator_device else ['normal','tsan','checked']):
 exe=out/('test-'+mode);cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module']
 if mode=='tsan':cmd+=['-fsanitize=thread']
 selected=(['-DCHECKED_TEST']+objects) if mode=='checked' else list(map(str,sources))
 compile_result=subprocess.run(cmd+selected+list(map(str,guards))+[str(test)]+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(compile_result.stdout+compile_result.stderr);assert not compile_result.returncode,compile_result.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 if mode=='checked' and not a.simulator_device:
  for fault in ['read','write']:
   bad=subprocess.run([str(exe),fault],capture_output=True,text=True,timeout=60);(out/(mode+'-'+fault+'.log')).write_text(bad.stdout+bad.stderr);assert bad.returncode==-6 and 'checked guest memory violation' in bad.stderr,(bad.returncode,bad.stderr)
 runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
report={'state':'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'runs':runs,'sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources+[test,pathlib.Path(__file__).resolve()]},'data_sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [R/'port/core/cp1252_table.inc',R/'port/core/nls_classification.inc',R/'port/core/nls_casing.inc',R/'port/core/nls_composition.inc',R/'config/encoding-sources.json']},'bindings_sha256':hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest(),'scope':'Actual ARM64 emitted stdcall service-wrapper execution; no QoL ARM64 module/game-session claim'};report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
