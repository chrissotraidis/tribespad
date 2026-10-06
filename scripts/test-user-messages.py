#!/usr/bin/env python3
"""Actual generated USER32 ABI, deadline/queue/callback semantics and barriers."""
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/user-messages';out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('binding_generator',R/'scripts/gen-win32-bindings.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
callback=out/'callback.ll';callback.write_text(g.generate({'TestTimerProc':dict(helper='test_timer_proc',arguments=4,returns=False,guest_base=False,cpu_argument=True)},False))
sources=[R/'tests/user_messages_test.c',callback,R/'tests/fixtures/native_enter.ll',R/'port/core/guest_memory_checks.c',R/'port/llasm-support/tribes_absolute_memory.c'];flags=['-DPTROFS_64BIT','-Wno-override-module'];sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();runs=[]
for mode in ['normal','checked','tsan']:
 build=R/'generated/native-win32'/('checked-macos' if mode=='checked' else 'macos');meta=json.loads((build/'result.json').read_text());objects=[]
 for name,row in meta['objects'].items():
  if name=='guest_dispatch' or (mode=='tsan' and name in ['guest_user_messages','guest_callback']):continue
  obj=build/(name+'.o');assert sha(obj)==row['object_sha256'];objects.append(str(obj))
 if mode=='checked':new,_=compile_support(sources,out/'checked',flags)
 else:new=list(map(str,sources))
 if mode=='tsan':new += [str(R/'port/core/guest_user_messages.c'),str(R/'port/core/guest_callback.c')]
 exe=out/('native-'+mode);cmd=['clang','-isysroot',sdk,'-target','arm64-apple-macos12','-O2']+(['-fsanitize=thread'] if mode=='tsan' else [])+flags+new+objects+[str(R/'generated/softfloat/macos/softfloat.a'),'-framework','Foundation','-framework','AppKit','-o',str(exe)]
 c=subprocess.run(cmd,capture_output=True,text=True);(out/(mode+'-build.log')).write_text(c.stdout+c.stderr);assert not c.returncode,c.stderr
 r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20);(out/(mode+'-run.log')).write_text(r.stdout+r.stderr);assert not r.returncode,(r.returncode,r.stderr)
 faults=[]
 if mode=='checked':
  for case in ['readonly','unreadable']:
   r=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=20);assert r.returncode==-6 and 'checked guest memory violation' in r.stderr,(case,r.returncode,r.stderr);faults.append(case)
 runs.append(dict(mode=mode,passed=True,binary_sha256=sha(exe),faults=faults));print(mode,'PASS',flush=True)
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=runs,source_sha256={str(p.relative_to(R)):sha(p) for p in sources+[R/'port/core/guest_user_messages.c',R/'port/core/guest_user_messages.h',R/'port/core/hidden_windows.c',R/'port/core/guest_window_catalog.c',R/'port/llasm-runtime/win32-bindings.json',pathlib.Path(__file__).resolve()]},scope='Component real deadlines and owning-thread original-width callbacks/queues, checked MSG read/write barriers, TSan queue/callback implementation. Keyboard/native input and full-engine timer delivery require separate acceptance.'),indent=2)+'\n')
