#!/usr/bin/env python3
"""Test native timer profile through emitted ABI, including both compiler guards."""
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/multimedia-timer';out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';data=json.loads(bindings.read_text());wrapper=out/'bindings.ll'
wrapper.write_text(gen.generate({n:data[n] for n in ['timeGetDevCaps','timeBeginPeriod','timeEndPeriod','timeGetTime']},False))
sources=[R/'port/core/multimedia_timer.c',R/'port/core/performance_clock.c',R/'port/core/guest_heap.c',R/'tests/fixtures/multimedia_timer_dispatch.c',R/'tests/fixtures/native_enter.ll',wrapper]
fixture=out/'contracts.ll';fixture.write_text('''declare i32 @clock_getres(i32,ptr)
define void @timer_guest_pointer(ptr %p) { %r=call i32 @clock_getres(i32 6,ptr %p)
 ret void
}
define void @timer_wrong_clock() { %r=call i32 @clock_getres(i32 0,ptr null)
 ret void
}
''')
wrong=out/'wrong-abi.ll';wrong.write_text('''declare i64 @clock_getres(i32,ptr)
define void @timer_wrong_abi() { %r=call i64 @clock_getres(i32 6,ptr null)
 ret void
}
''')
objects,checked=compile_support(sources+[fixture,wrong],out/'checked',['-DPTROFS_64BIT'])
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded')
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/multimedia_timer_test.c'
report=dict(passed=False,checked_sha256=checked,guarded_sha256=guarded)
for mode in ['normal','tsan','checked']:
 exe=out/('test-'+mode);cmd=['clang','-target','arm64-apple-macos12','-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*map(str,guards),str(test)]
 cmd+=(['-DCHECKED_TEST']+objects) if mode=='checked' else list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==0 and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
 if mode=='checked':
  assert run.stderr.count('checked guest memory violation')==2,run.stderr
  assert run.stderr.count('unverified call: clock_getres')==2,run.stderr
  assert 'clock_getres' in run.stderr
 else:assert not run.stderr,run.stderr
 report[mode]=dict(passed=True,binary_sha256=sha(exe),stdout=run.stdout);print(mode,run.stdout.strip())
injected=out/'test-errors';error_source=R/'tests/multimedia_timer_errors_test.c'
subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-Dclock_getres=timer_fake_resolution',str(R/'port/core/multimedia_timer.c'),str(error_source),'-o',str(injected)],check=True)
run=subprocess.run([str(injected)],capture_output=True,text=True,check=True);assert run.stdout.startswith('PASS:') and not run.stderr
(out/'errors.log').write_text(run.stdout);report['injected']=dict(passed=True,binary_sha256=sha(injected),stdout=run.stdout);print(run.stdout.strip())
paths=sources+guards+[fixture,wrong,test,error_source,bindings,R/'port/core/multimedia_timer.h',R/'config/runtime/multimedia-timer.json',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]
report.update(passed=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Real native monotonic clock resolution, declared millisecond period range and synchronized matching references; emitted ABI, LastError, bounds, sleep and both compiler guards. No callback timer, scheduler deadline, mobile runtime or game-loop acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
