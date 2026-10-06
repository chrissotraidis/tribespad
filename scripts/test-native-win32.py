#!/usr/bin/env python3
"""Exercise synchronization, guest allocation and descriptor handles under contention."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/native-win32/tests';out.mkdir(parents=True,exist_ok=True)
ap=argparse.ArgumentParser();ap.add_argument('--only',nargs='+');ap.add_argument('--report-directory',type=pathlib.Path,help='Isolated selected-test report; never reuses the default aggregate');a=ap.parse_args()
if a.report_directory:
 assert a.only,'isolated report requires an explicit test selection'
 out=(R/a.report_directory).resolve();out.mkdir(parents=True,exist_ok=True)
 assert out!=R/'generated/native-win32/tests','use a separate report directory'
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
results=json.loads((out/'result.json').read_text())['tests'] if a.only and not a.report_directory else {}
if a.only and not a.report_directory:
 assert set(a.only)<=set(results)
 for name,row in results.items():
  if name in a.only:continue
  source=R/('port/llasm-support' if name=='tribes_result' else 'port/core')/(('guest_dinput' if name=='guest_dinput_abi' else 'native_handles' if name=='native_semaphore' else name)+'.c')
  assert hashlib.sha256(source.read_bytes()).hexdigest()==row['source_sha256'],name
  assert hashlib.sha256((R/'tests'/(name+'_test.c')).read_bytes()).hexdigest()==row['test_sha256'],name
  for path,digest in row['dependency_sha256'].items():assert hashlib.sha256((R/path).read_bytes()).hexdigest()==digest,(name,path)
(out/'result.json').write_text(json.dumps({'pass':False,'state':'running'})+'\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');binding_generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(binding_generator);all_bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());abi_wrapper=out/'directinput-bindings.ll';abi_wrapper.write_text(binding_generator.generate({k:v for k,v in all_bindings.items() if k in ['DirectInputCreateA','DI_QueryInterface','DI_AddRef','DI_Release','DI_Initialize','DI_EnumDevices','DI_CreateDevice','DID_QueryInterface','DID_AddRef','DID_Release','DID_GetCapabilities','DID_GetDeviceInfo','DID_EnumObjects','DID_GetProperty','DID_SetDataFormat','DID_SetProperty']}))
subprocess.run(['python3',str(R/'scripts/gen-codepage-table.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-classification.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-casing.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-cursor-theme.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-keyboard-layout.py'),'--check'],check=True)
for name in ['guest_encoding','native_fls','process_heap','guest_file','guest_find','guest_display','guest_cursors','guest_icons','keyboard_state','keyboard_translation','input_buffer','guest_dinput_abi','guest_dinput','guest_version','guest_cpu','critical_section','guest_heap','native_handles','native_semaphore','process_environment','process_startup','guest_modules','gdi_memory','window_classes','hidden_windows','native_winsock','guest_tls','performance_clock','guest_filesystem','tribes_result']:
 if a.only and name not in a.only:continue
 source=R/('port/llasm-support' if name=='tribes_result' else 'port/core')/(('guest_dinput' if name=='guest_dinput_abi' else 'native_handles' if name=='native_semaphore' else name)+'.c');test=R/'tests'/(name+'_test.c');rows=[]
 for mode in ['normal','tsan']:
  exe=out/(name+'-'+mode);cmd=['clang','-isysroot',sdk,'-arch','arm64','-O1' if mode=='tsan' else '-O2','-g','-std=c11','-Wall','-Wextra','-Werror','-pthread','-DPTROFS_64BIT','-Wno-override-module']
  if mode=='tsan':cmd+=['-fsanitize=thread']
  extras=[str(R/'port/core/guest_heap.c')] if name in ['guest_encoding','native_fls','process_heap','critical_section','guest_file','guest_find','guest_display','guest_cursors','guest_icons','keyboard_state','input_buffer','guest_dinput_abi','guest_dinput','guest_version','native_handles','native_semaphore','process_environment','process_startup','guest_modules','gdi_memory','window_classes','hidden_windows','native_winsock','guest_tls','performance_clock','guest_filesystem','tribes_result'] else []
  if name=='gdi_memory':extras += [str(R/'port/apple/font.m'),str(R/'port/core/guest_memory_checks.c')];cmd += ['-framework','CoreText','-framework','CoreGraphics','-framework','CoreFoundation']
  if name=='process_heap':extras.append(str(R/'port/core/guest_virtual_memory.c'))
  if name=='guest_file':extras += [str(R/'port/core/guest_filesystem.c'),str(R/'port/core/native_handles.c')]
  if name=='guest_find':extras.append(str(R/'port/core/guest_filesystem.c'))
  if name=='keyboard_state':extras.append(str(R/'port/core/keyboard_translation.c'))
  if name=='keyboard_translation':extras += [str(R/'port/core/keyboard_state.c'),str(R/'port/core/guest_heap.c')]
  if name in ['guest_dinput','guest_dinput_abi']:extras.append(str(R/'port/core/input_buffer.c'))
  if name=='guest_dinput':extras += [str(R/'port/core'/(n+'.c')) for n in ['guest_modules','native_exports','guest_callback','guest_dispatch']]+[str(R/'tests/fixtures/native_exports.c'),str(R/'tests/fixtures/native_enter.ll')]
  if name=='guest_dinput_abi':extras += [str(R/'port/core'/(n+'.c')) for n in ['guest_modules','native_exports','guest_dispatch','guest_callback']]+[str(R/'tests/fixtures/native_enter.ll'),str(R/'tests/fixtures/dinput_callback.ll'),str(abi_wrapper)]
  if name=='window_classes':extras.append(str(R/'port/core/guest_dispatch.c'))
  if name=='hidden_windows':extras += [str(R/'port/core'/(n+'.c')) for n in ['guest_display','window_classes','guest_callback','guest_dispatch','guest_window_catalog']]+[str(R/'tests/fixtures/callback.ll')]
  if name=='process_environment':extras.append(str(R/'port/core/guest_filesystem.c'))
  if name=='process_startup':extras.append(str(R/'port/core/native_handles.c'))
  compilation=subprocess.run(cmd+[str(source),*extras,str(test),'-o',str(exe)],capture_output=True,text=True);(out/(name+'-'+mode+'-compile.log')).write_text(compilation.stdout+compilation.stderr)
  assert compilation.returncode==0,(name,mode,compilation.stdout,compilation.stderr)
  run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/(name+'-'+mode+'.log')).write_text(run.stdout+run.stderr)
  assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(name,mode,run.returncode,run.stderr)
  rows.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
 results[name]={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'test_sha256':hashlib.sha256(test.read_bytes()).hexdigest(),'dependency_sha256':{str(pathlib.Path(src).relative_to(R)):hashlib.sha256(pathlib.Path(src).read_bytes()).hexdigest() for src in extras},'runs':rows}
report={'pass':True,'tests':results,'scope':'Native service tests including synchronization, allocation, handles, environment, launch metadata, module metadata, native provider load/export references, native DirectInput COM roots and actual emitted vtable ABI calls, CPU/version profiles, memory DCs/bitmaps, window classes, native reentrant callbacks, hidden windows, BSD UDP socket transport, TLS, boot/monotonic/UTC clocks, existing-path mounts, disk geometry and wide-result buffers; no full Windows or game-session claim'}
if a.report_directory:
 assert set(results)==set(a.only),'unknown or missing selected test'
 report.update(scope='Selected native regressions only; no historical unrelated report reuse or complete game-session claim',cpu_header_sha256=hashlib.sha256((R/'port/llasm-support/llasm_cpu.h').read_bytes()).hexdigest())
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
