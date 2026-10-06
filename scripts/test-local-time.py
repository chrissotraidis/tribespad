#!/usr/bin/env python3
"""Exercise the reached void SYSTEMTIME service through its emitted guest ABI."""
import hashlib,importlib.util,json,os,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/local-time';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({'GetLocalTime':bindings['GetLocalTime']}))
lookup=out/'lookup.ll';lookup.write_text('''declare hidden fastcc void @tbimp_GetLocalTime(ptr)
declare fastcc void @tribes_return_to_host(ptr)
declare void @abort() noreturn
define ptr @tribes_lookup(i32 %pc) {
 switch i32 %pc, label %bad [i32 1, label %local i32 4294963200, label %host]
local: ret ptr @tbimp_GetLocalTime
host: ret ptr @tribes_return_to_host
bad: call void @abort()
 unreachable
}
''')
enter=R/'tests/fixtures/native_enter.ll'
support=[R/'port/core/guest_datetime.c',R/'port/core/guest_heap.c',wrapper,lookup,enter]
objects,_=compile_support(support,out/'checked',[]);meta=json.loads((out/'checked/result.json').read_text())
entries=[dict(object=str(p),object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']) for p in map(pathlib.Path,objects)]
guarded,_=guard_link(entries,out/'guarded')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
fixture=R/'tests/local_time_test.c';barrier=R/'port/core/guest_memory_checks.c';external=R/'port/core/guest_external_checks.c'
flags=['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module'];runs=[]
for mode,inputs in [('normal',support),('checked',guarded)]:
 exe=out/(mode+'-test');subprocess.run(flags+list(map(str,[fixture,barrier,external,*inputs]))+['-o',str(exe)],check=True)
 for timezone in ['UTC','America/Chicago','Asia/Kathmandu']:
  run=subprocess.run([str(exe)],env={**os.environ,'TZ':timezone},capture_output=True,text=True,timeout=20);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(mode,timezone,run.stdout,run.stderr)
  runs.append(dict(mode=mode,timezone=timezone,output=run.stdout.strip(),binary_sha256=sha(exe)))
 print('PASS:',mode,'actual local time in UTC, Chicago and Kathmandu',flush=True)
faults=[]
for name in ['null','short','readonly']:
 run=subprocess.run([str(out/'checked-test'),name],capture_output=True,text=True,timeout=20);(out/(name+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==-6 and ('GetLocalTime output range' in run.stderr if name=='null' else 'memory violation' in run.stderr),(name,run.returncode,run.stderr)
 faults.append(dict(name=name,returncode=run.returncode))
paths=[pathlib.Path(__file__).resolve(),fixture,R/'port/core/guest_datetime.h',*support[:2],enter,barrier,external,R/'port/llasm-runtime/win32-bindings.json']
(out/'result.json').write_text(json.dumps(dict(passed=True,state='finished',runs=runs,checked_faults=faults,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Native and checked emitted GetLocalTime ABI, actual host timezone/calendar/millisecond output and guarded 16-byte writes. Component evidence; not original-game or playable-map acceptance.'),indent=2)+'\n')
