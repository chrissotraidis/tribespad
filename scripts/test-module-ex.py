#!/usr/bin/env python3
"""Validate retained module lookups with native concurrency and both guards."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [v['udid'] for rows in d['devices'].values() for v in rows if v['state']=='Booted']==[a.simulator_device]
root=R/'generated/integration-review/module-ex';out=root/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True)
(out/'result.json').write_text(json.dumps(dict(passed=False,state='running'))+'\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';data=json.loads(bindings.read_text());wrapper=out/'bindings.ll'
wrapper.write_text(gen.generate({n:data[n] for n in ['GetModuleHandleExW','GetModuleHandleExA','FreeLibrary']},False))
sources=[R/'port/core/guest_modules.c',R/'port/core/guest_heap.c',R/'tests/fixtures/module_ex_dispatch.c',R/'tests/fixtures/native_enter.ll',wrapper]
sources += [R/'port/core'/(n+'.c') for n in ['guest_loader','guest_threads','guest_callback','guest_virtual_memory','guest_teb','guest_thread_ids','native_handles','native_fls','guest_fls','shift']] + [R/'port/llasm-support/tribes_absolute_memory.c',R/'port/llasm-support/tribes_shift.c']
objects=[];checked=guarded=None
if not a.simulator_device:
 objects,checked=compile_support(sources,out/'checked',['-DPTROFS_64BIT']);meta=json.loads((out/'checked/result.json').read_text());entries=[]
 for obj in objects:
  p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
 objects,guarded=guard_link(entries,out/'guarded')
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/module_ex_test.c';report=dict(passed=False,checked_sha256=checked,guarded_sha256=guarded,simulator_device=a.simulator_device,runs=[])
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
for mode in (['normal'] if a.simulator_device else ['normal','tsan','checked']):
 exe=out/('test-'+mode);cmd=['clang','-target',target,'-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*map(str,guards),str(test)]
 cmd+=(['-DCHECKED_TEST']+objects) if mode=='checked' else list(map(str,sources))
 if mode=='tsan' or a.simulator_device:cmd+=['-DTSAN_TEST'] # Fork faults run only in normal/checked host builds.
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True)
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=90);(out/(mode+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==0 and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
 expected=0 if mode=='tsan' or a.simulator_device else 6 if mode=='checked' else 2
 assert run.stderr.count('TribesPad:')==expected,run.stderr
 if mode=='checked':assert run.stderr.count('checked guest memory violation')==4,run.stderr
 report['runs'].append(dict(mode=mode,passed=True,binary_sha256=sha(exe),stdout=run.stdout));print(mode,run.stdout.strip(),flush=True)
paths=sources+guards+[test,bindings,R/'port/core/guest_modules.h',R/'config/runtime/module-references.json',pathlib.Path(__file__).resolve()]
report.update(passed=True,state='finished',sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Actual emitted guest ABI and native concurrent module reference accounting. Audited PE extents; no dynamic PE loading/unloading, detach callbacks, guest thread or game acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
