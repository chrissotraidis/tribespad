#!/usr/bin/env python3
"""Native guest-thread lifecycle through finite AOT stdcall fixtures."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess,signal
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
a=argparse.ArgumentParser();a.add_argument('--simulator-device');a.add_argument('--modes',default='normal,tsan,checked');a=a.parse_args()
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [v['udid'] for rows in d['devices'].values() for v in rows if v['state']=='Booted']==[a.simulator_device]
out=R/'generated/integration-review/guest-threads'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True)
(out/'result.json').write_text(json.dumps(dict(passed=False,state='running'))+'\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';data=json.loads(bindings.read_text());wrapper=out/'bindings.ll'
wrapper.write_text(gen.generate({n:data[n] for n in ['QueueUserWorkItem','CreateThread','ResumeThread','GetExitCodeThread','GetThreadId','ExitThread','FreeLibraryAndExitThread']},False))
sources=[R/'port/core'/(n+'.c') for n in ['guest_threads','guest_callback','guest_heap','guest_modules','guest_virtual_memory','guest_teb','guest_thread_ids','native_handles','native_fls','guest_fls','shift']]+[R/'port/llasm-support/tribes_absolute_memory.c',R/'port/llasm-support/tribes_shift.c',R/'tests/fixtures/peb_readers.ll',R/'tests/fixtures/guest_threads_dispatch.c',R/'tests/fixtures/guest_threads.ll',R/'tests/fixtures/native_enter.ll',wrapper]
objects=[];checked=guarded=None;modes=['normal'] if a.simulator_device else a.modes.split(',')
if 'checked' in modes:
 objects,checked=compile_support(sources,out/'checked',['-DPTROFS_64BIT']);meta=json.loads((out/'checked/result.json').read_text());entries=[]
 for obj in objects:
  p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
 objects,guarded=guard_link(entries,out/'guarded')
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/guest_threads_test.c';report=dict(passed=False,checked_sha256=checked,guarded_sha256=guarded,simulator_device=a.simulator_device,runs=[])
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
for mode in modes:
 exe=out/('test-'+mode);cmd=['clang','-target',target,'-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*map(str,guards),str(test)]
 cmd+=(['-DCHECKED_TEST']+objects) if mode=='checked' else list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True)
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(mode,run.returncode,run.stdout,run.stderr)
 if not a.simulator_device and mode!='tsan':
  fault=subprocess.run([str(exe),'guard'],capture_output=True,text=True,timeout=30);(out/(mode+'-guard.log')).write_text(fault.stdout+fault.stderr)
  if mode=='checked':assert fault.returncode==-signal.SIGABRT and 'checked guest memory violation' in fault.stderr,(fault.returncode,fault.stderr)
  else:assert fault.returncode in (-signal.SIGBUS,-signal.SIGSEGV),(fault.returncode,fault.stderr)
 if mode=='checked':
  for fault_id in range(1,5):
   fault=subprocess.run([str(exe),str(fault_id)],capture_output=True,text=True,timeout=30);(out/('host-fault-%d.log'%fault_id)).write_text(fault.stdout+fault.stderr)
   assert fault.returncode==-signal.SIGABRT and 'external memory contract stop' in fault.stderr,(fault_id,fault.returncode,fault.stderr)
 report['runs'].append(dict(mode=mode,passed=True,binary_sha256=sha(exe),stdout=run.stdout));print(mode,run.stdout.strip(),flush=True)
paths=sources+guards+[test,bindings,pathlib.Path(__file__).resolve()]
report.update(passed=True,state='finished',sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Native guest-thread lifecycle via finite AOT fixtures, not original-game or physical-device acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
