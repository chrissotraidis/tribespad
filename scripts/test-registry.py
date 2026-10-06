#!/usr/bin/env python3
"""Execute the real hardware registry through its checked emitted x86 ABI."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator');a=ap.parse_args()
if a.simulator:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator else ('macosx','arm64-apple-macos12')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/registry'/('simulator' if a.simulator else 'macos');out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=R/'port/llasm-runtime/win32-bindings.json';wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({name:json.loads(bindings.read_text())[name] for name in ['RegOpenKeyExW','RegQueryValueExW','RegCloseKey','RegOpenKeyExA','RegGetValueA']},False))
subprocess.run(['python3',str(R/'scripts/gen-cpu-metadata.py'),'--check'],check=True)
sources=[R/n for n in ['port/core/guest_registry.c','port/core/guest_heap.c','tests/fixtures/registry_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
contracts=out/'sysctl-contracts.ll';contracts.write_text('''declare i32 @sysctlbyname(ptr,ptr,ptr,ptr,i64)
@name = private constant [8 x i8] c"hw.ncpu\\00"
define void @registry_guest_sysctl(ptr %p) {
 %r = call i32 @sysctlbyname(ptr @name,ptr %p,ptr %p,ptr null,i64 0)
 ret void
}
define void @registry_write_sysctl(ptr %p) {
 %r = call i32 @sysctlbyname(ptr @name,ptr null,ptr null,ptr %p,i64 4)
 ret void
}
''')
wrong=out/'sysctl-wrong.ll';wrong.write_text('''declare i32 @sysctlbyname(ptr,ptr,ptr,ptr,i32)
@name = private constant [8 x i8] c"hw.ncpu\\00"
define void @registry_wrong_sysctl(ptr %p) {
 %r = call i32 @sysctlbyname(ptr @name,ptr %p,ptr %p,ptr null,i32 0)
 ret void
}
''')
extra=[contracts,wrong]
objects,checked=compile_support(sources+extra,out/'checked',['-DPTROFS_64BIT'],target=target,sdk_name=sdk_name);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target);test=R/'tests/registry_test.c';guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];report=dict(passed=False,checked_sha256=checked,guarded_sha256=guarded,simulator=a.simulator,target=target)
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
for mode in (['checked'] if a.simulator else ['checked','normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',str(test),*map(str,guards)]
 if a.simulator:cmd+=['-DSIMULATOR_TEST']
 if mode=='checked':exe=out/'test-checked';cmd+=['-DCHECKED_TEST']+objects
 else:cmd+=list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True);run=subprocess.run(['xcrun','simctl','spawn',a.simulator,str(exe)] if a.simulator else [str(exe)],capture_output=True,text=True,timeout=45)
 (out/(mode+'-stdout.log')).write_text(run.stdout);(out/(mode+'-stderr.log')).write_text(run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
 if mode=='checked' and not a.simulator:
  assert run.stderr.count('checked guest memory violation')==10,run.stderr
  assert run.stderr.count('registry namespace not implemented')==3,run.stderr
  assert run.stderr.count('user registry namespace not implemented')==1,run.stderr
  assert run.stderr.count('guest pointer passed to host-only call: sysctlbyname')==1,run.stderr
  assert run.stderr.count('unverified call: sysctlbyname')==2,run.stderr
 if mode=='tsan':assert not run.stderr
 report[mode]=dict(passed=True,binary_sha256=sha(exe),stdout=run.stdout);print(mode,run.stdout.strip())
report.update(passed=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+extra+[test,*guards,R/'port/core/guest_registry.h',R/'port/core/cpu_metadata_profile.h',R/'config/runtime/cpu-metadata.json',R/'scripts/gen-cpu-metadata.py',R/'ref/downloads/libuv-cpu-reference/darwin.c',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]},scope='Read-only HKLM hardware CPU metadata from host sysctl; missing frequency uses the declared 2400 MHz libuv Apple compatibility fallback, not a physical measurement. Checked emitted ABI, handles, buffer sizes and host-only read contracts. No persisted software registry, device execution or game acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
