#!/usr/bin/env python3
"""Exercise original ABI wrappers and real native condition-variable contention."""
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/condition-variables';out.mkdir(parents=True,exist_ok=True)
(out/'result.json').write_text(json.dumps(dict(passed=False,state='running'))+'\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';data=json.loads(bindings.read_text());wrapper=out/'bindings.ll'
wrapper.write_text(gen.generate({n:data[n] for n in ['InitializeConditionVariable','WakeConditionVariable','WakeAllConditionVariable','SleepConditionVariableSRW','SleepConditionVariableCS']},False))
sources=[R/'port/core/guest_condition.c',R/'port/core/guest_srw.c',R/'port/core/critical_section.c',R/'port/core/guest_heap.c',R/'tests/fixtures/condition_dispatch.c',R/'tests/fixtures/native_enter.ll',wrapper]
fixture=out/'contracts.ll';fixture.write_text('''declare i32 @pthread_cond_timedwait_relative_np(ptr,ptr,ptr)
declare i32 @pthread_cond_signal(ptr)
define void @condition_guest_pointer(ptr %c,ptr %m,ptr %t) { %r=call i32 @pthread_cond_timedwait_relative_np(ptr %c,ptr %m,ptr %t)
 ret void
}
define void @condition_signal_guest(ptr %c) { %r=call i32 @pthread_cond_signal(ptr %c)
 ret void
}
''')
wrong=out/'wrong-abi.ll';wrong.write_text('''declare i64 @pthread_cond_timedwait_relative_np(ptr,ptr,ptr)
declare i64 @pthread_cond_signal(ptr)
define void @condition_wrong_abi() { %r=call i64 @pthread_cond_timedwait_relative_np(ptr null,ptr null,ptr null)
 ret void
}
define void @condition_signal_wrong() { %r=call i64 @pthread_cond_signal(ptr null)
 ret void
}
''')
objects,checked=compile_support(sources+[fixture,wrong],out/'checked',['-DPTROFS_64BIT'])
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded')
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/condition_test.c'
report=dict(passed=False,checked_sha256=checked,guarded_sha256=guarded)
for mode in ['normal','tsan','checked']:
 exe=out/('test-'+mode);cmd=['clang','-target','arm64-apple-macos12','-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*map(str,guards),str(test)]
 cmd+=(['-DCHECKED_TEST']+objects) if mode=='checked' else list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=90);(out/(mode+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==0 and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
 if mode=='checked':
  assert run.stderr.count('checked guest memory violation')==3,run.stderr
  assert run.stderr.count('unverified call:')==2,run.stderr
 else:assert not run.stderr,run.stderr
 report[mode]=dict(passed=True,binary_sha256=sha(exe),stdout=run.stdout);print(mode,run.stdout.strip(),flush=True)
report['faults']=[]
for case in ['null','overflow','nonzero','unowned','mode','flags','recursive-cs','unowned-cs','foreign','busy-init','copied-live','delete-cs']:
 run=subprocess.run([str(out/'test-normal'),case],capture_output=True,text=True,timeout=20);(out/(case+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==-6 and 'TribesPad:' in run.stderr,(case,run.returncode,run.stderr)
 report['faults'].append(dict(case=case,returncode=run.returncode,stderr=run.stderr))
paths=sources+guards+[fixture,wrong,test,bindings,R/'port/core/guest_condition.h',R/'port/core/critical_section.h',R/'port/core/guest_srw.h',R/'config/runtime/condition-variables.json',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]
report.update(passed=True,state='finished',sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Native Apple threads and emitted guest API wrappers. No guest thread-creation, menu, game-loop, mobile runtime or authenticated-match acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: normal/TSan/checked, nine compiler guards and twelve invalid lifetime/ownership cases')
