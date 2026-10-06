#!/usr/bin/env python3
"""Execute the real per-processor query through its checked emitted x86 ABI."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator');a=ap.parse_args()
if a.simulator:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator else ('macosx','arm64-apple-macos12')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/system-query'/('simulator' if a.simulator else 'macos');out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=R/'port/llasm-runtime/win32-bindings.json';wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({'NtQuerySystemInformation':json.loads(bindings.read_text())['NtQuerySystemInformation']},False))
sources=[R/n for n in ['port/core/guest_system_query.c','port/core/guest_heap.c','tests/fixtures/system_query_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
# Separate LLVM fixtures exercise source/ABI gates and integer-encoded pointers.
source_name='source_filename = '+json.dumps(str(R/'port/core/guest_system_query.c'))+'\n'
contracts=out/'mach-contracts.ll';contracts.write_text(source_name+'''declare i32 @host_processor_info(i32,i32,ptr,ptr,ptr)
declare i32 @vm_deallocate(i32,i64,i64)
define void @system_query_guest_count(ptr %p) {
 %r = call i32 @host_processor_info(i32 0,i32 2,ptr %p,ptr %p,ptr %p)
 ret void
}
define void @system_query_guest_vm(ptr %p) {
 %a = ptrtoint ptr %p to i64
 %r = call i32 @vm_deallocate(i32 0,i64 %a,i64 4096)
 ret void
}
''')
wrong=out/'mach-wrong.ll';wrong.write_text(source_name+'''declare i64 @mach_host_self()
define void @system_query_wrong_abi() {
 %r = call i64 @mach_host_self()
 ret void
}
''')
unreviewed=out/'mach-unreviewed.ll';unreviewed.write_text('''declare i32 @mach_host_self()
define void @system_query_unreviewed() {
 %r = call i32 @mach_host_self()
 ret void
}
''')
extra=[contracts,wrong,unreviewed]
objects,checked=compile_support(sources+extra,out/'checked',['-DPTROFS_64BIT'],target=target,sdk_name=sdk_name);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target);test=R/'tests/system_query_test.c';guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];report=dict(passed=False,checked_sha256=checked,guarded_sha256=guarded,simulator=a.simulator,target=target)
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
for mode in (['checked'] if a.simulator else ['checked','normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',str(test),*map(str,guards)]
 if a.simulator:cmd+=['-DSIMULATOR_TEST']
 if mode=='checked':exe=out/'test-checked';cmd+=['-DCHECKED_TEST']+objects
 else:cmd+=list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True);run=subprocess.run(['xcrun','simctl','spawn',a.simulator,str(exe)] if a.simulator else [str(exe)],capture_output=True,text=True,timeout=45)
 (out/(mode+'-stdout.log')).write_text(run.stdout);(out/(mode+'-stderr.log')).write_text(run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
 if mode=='tsan':assert not run.stderr
 report[mode]=dict(passed=True,binary_sha256=sha(exe),stdout=run.stdout);print(mode,run.stdout.strip())
report.update(passed=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+extra+[test,*guards,R/'port/core/guest_system_query.h',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]},scope='Class8 real Mach per-CPU counters, 32-CPU guest cap, partial/full x86 records, generated ABI and concurrency. Reserved Windows counters zero. Simulator is a command-line component. Other NT information classes remain explicit stops.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
