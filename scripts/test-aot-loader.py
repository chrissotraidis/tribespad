#!/usr/bin/env python3
import argparse,hashlib,importlib.util,json,pathlib,subprocess,signal
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 devices=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in devices['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
out=R/'generated/integration-review/aot-loader/components'
if a.simulator_device:out=out/a.simulator_device
out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
b=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(g.generate({n:b[n] for n in ['LoadLibraryA','LoadLibraryExA','LoadLibraryExW','FreeLibrary','DisableThreadLibraryCalls']},False))
bridge=out/'fixture.ll';bridge.write_text('declare void @loader_fixture_dll(ptr)\ndeclare void @loader_fixture_worker(ptr)\ndefine fastcc void @loader_dll(ptr %c){call void @loader_fixture_dll(ptr %c)\nret void}\ndefine fastcc void @loader_worker(ptr %c){call void @loader_fixture_worker(ptr %c)\nret void}\n')
sources=[R/'port/core'/(n+'.c') for n in ['guest_loader','guest_modules','guest_threads','guest_callback','guest_heap','guest_virtual_memory','guest_teb','guest_thread_ids','native_handles','native_fls','guest_fls','shift']]+[R/'port/llasm-support/tribes_absolute_memory.c',R/'port/llasm-support/tribes_shift.c',R/'tests/fixtures/native_enter.ll',R/'tests/aot_loader_test.c',wrapper,bridge]
runs=[]
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/('test-'+mode);cmd=['clang','-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*map(str,sources),'-o',str(exe)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 if a.simulator_device:cmd+=['-target','arm64-apple-ios16-simulator','-isysroot',subprocess.check_output(['xcrun','--sdk','iphonesimulator','--show-sdk-path'],text=True).strip()]
 subprocess.run(cmd,check=True);run=subprocess.run((['xcrun','simctl','spawn',a.simulator_device] if a.simulator_device else [])+[str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0,(run.returncode,run.stdout,run.stderr)
 assert 'WARNING: ThreadSanitizer' not in run.stderr
 if mode=='normal' and not a.simulator_device:
  for fault,text in [('unload','mapped image unload'),('reload','unsupported dynamic image state/reload')]:
   fail=subprocess.run([str(exe),fault],capture_output=True,text=True,timeout=60);assert fail.returncode==-signal.SIGABRT and text in fail.stderr,(fault,fail.returncode,fail.stderr);(out/(fault+'.log')).write_text(fail.stderr)
 runs.append(dict(mode=mode,passed=True,binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest()));print(mode,run.stdout,flush=True)
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=runs,scope='Finite loader via emitted ABI/native callback fixtures, not original codec decoding. Final unload and failed-image reload remain explicit stops.',sources_sha256={str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources+[pathlib.Path(__file__).resolve()]}),indent=2)+'\n')
