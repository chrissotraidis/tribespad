#!/usr/bin/env python3
"""Exercise real Apple niceness and declared guest affinity through emitted ABI."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator');a=ap.parse_args()
if a.simulator:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator else ('macosx','arm64-apple-macos12')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/scheduling'/('simulator' if a.simulator else 'macos');out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';data=json.loads(bindings.read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({n:data[n] for n in ['SetPriorityClass','GetProcessAffinityMask','SetThreadAffinityMask']},False))
sources=[R/'port/core/guest_scheduling.c',R/'port/core/guest_heap.c',R/'tests/fixtures/scheduling_dispatch.c',R/'tests/fixtures/native_enter.ll',wrapper]
# Test-only source attribution lets each ABI/selector restriction be exercised
# independently of the separate unreviewed-caller rejection. Not a production input.
fixture=out/'scheduling-contracts.ll';fixture.write_text('source_filename = "'+str(R/'port/core/guest_scheduling.c')+'"\n'+'''declare i32 @setpriority(i32,i32,i32)
define void @scheduling_wrong_selector() {
 %r=call i32 @setpriority(i32 1,i32 0,i32 0)
 ret void
}
define void @scheduling_wrong_process() {
 %r=call i32 @setpriority(i32 0,i32 1,i32 0)
 ret void
}
''')
wrong=out/'scheduling-wrong.ll';wrong.write_text('source_filename = "'+str(R/'port/core/guest_scheduling.c')+'"\n'+'''declare i32 @setpriority(i32,i64,i32)
define void @scheduling_wrong_abi() {
 %r=call i32 @setpriority(i32 0,i64 0,i32 0)
 ret void
}
''')
unreviewed=out/'scheduling-unreviewed.ll';unreviewed.write_text('''declare i32 @setpriority(i32,i32,i32)
define void @scheduling_unreviewed() {
 %r=call i32 @setpriority(i32 0,i32 0,i32 0)
 ret void
}
''')
extra=[fixture,wrong,unreviewed]
objects,checked=compile_support(sources+extra,out/'checked',['-DPTROFS_64BIT'],target=target,sdk_name=sdk_name)
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target)
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/scheduling_test.c'
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();report=dict(passed=False,checked_sha256=checked,guarded_sha256=guarded,target=target,simulator=a.simulator)
for mode in (['checked'] if a.simulator else ['checked','normal','tsan']):
 exe=out/('test-'+mode);cmd=['clang','-target',target,'-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*map(str,guards),str(test)]
 cmd+=(['-DCHECKED_TEST']+objects) if mode=='checked' else list(map(str,sources))
 if a.simulator:cmd+=['-DSIMULATOR_TEST']
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True);runs=[]
 for priority in ['0x20','0x8000','0x80','0x4000','0x40']:
  args=[str(exe),priority];run=subprocess.run(['xcrun','simctl','spawn',a.simulator,*args] if a.simulator else args,capture_output=True,text=True,timeout=45)
  (out/(mode+'-'+priority+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
  if mode=='checked' and not a.simulator:assert run.stderr.count('checked guest memory violation')==2 and run.stderr.count('unverified call: setpriority')==4,run.stderr
  else:assert not run.stderr
  runs.append(run.stdout.strip());print(mode,run.stdout.strip())
 report[mode]=dict(passed=True,binary_sha256=sha(exe),runs=runs)
if not a.simulator:
 injected=out/'injected';injected_source=R/'tests/scheduling_errors_test.c'
 subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-Dscheduling_injected=1','-Dsetpriority=scheduling_injected_setpriority',str(R/'port/core/guest_scheduling.c'),str(R/'port/core/guest_heap.c'),str(injected_source),'-o',str(injected)],check=True)
 run=subprocess.run([str(injected)],capture_output=True,text=True,check=True);assert run.stdout.startswith('PASS:') and not run.stderr
 report['injected']=dict(passed=True,stdout=run.stdout,binary_sha256=sha(injected));print(run.stdout.strip())
sources+=extra+guards+[test,R/'tests/scheduling_errors_test.c',bindings,R/'config/runtime/scheduling.json',R/'port/core/guest_scheduling.h',R/'port/core/guest_system.h',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',R/'scripts/guard_checked_link.py',pathlib.Path(__file__).resolve()]
report.update(passed=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources},scope='Actual current-process niceness outcomes, finite guest group masks and honest unsupported strict affinity. No Windows scheduler parity, external process handles, real-time or background-mode support. Device compilation is separate.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
