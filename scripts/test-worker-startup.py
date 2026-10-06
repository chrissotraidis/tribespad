#!/usr/bin/env python3
"""Exercise event ABI/wakes on normal, checked and TSan runtime builds."""
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/worker-startup';out.mkdir(parents=True,exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('g',R/'scripts/gen-win32-bindings.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
wrapper=out/'handler.ll';wrapper.write_text(g.generate({'TestHandler':dict(helper='test_handler',arguments=4,returns=True,guest_base=False,cpu_argument=True,calling_convention='cdecl')},False))
sources=[wrapper,R/'port/core/guest_raise_exception.c',R/'tests/worker_startup_test.c',R/'tests/fixtures/native_enter.ll',R/'port/core/native_handles.c',R/'port/core/guest_memory_checks.c',R/'port/llasm-support/tribes_absolute_memory.c'];flags=['-DPTROFS_64BIT','-DTRIBESPAD_EVENT_OBSERVER','-Wno-override-module'];sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();runs=[]
for mode in ['normal','checked','tsan']:
 build=R/'generated/native-win32'/('checked-macos' if mode=='checked' else 'macos');meta=json.loads((build/'result.json').read_text());objects=[]
 for name,row in meta['objects'].items():
  if name in ['guest_dispatch','native_handles','guest_raise_exception']:continue
  obj=build/(name+'.o');assert sha(obj)==row['object_sha256'];objects.append(str(obj))
 if mode=='checked':new,_=compile_support(sources,out/'checked',flags)
 else:new=list(map(str,sources))
 exe=out/("native-"+mode);cmd=['clang','-isysroot',sdk,'-target','arm64-apple-macos12','-O2']+(['-fsanitize=thread'] if mode=='tsan' else [])+flags+new+objects+[str(R/'generated/softfloat/macos/softfloat.a'),'-framework','Foundation','-framework','AppKit','-o',str(exe)]
 c=subprocess.run(cmd,capture_output=True,text=True);(out/(mode+'-build.log')).write_text(c.stdout+c.stderr);assert not c.returncode,c.stderr
 r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/(mode+'-run.log')).write_text(r.stdout+r.stderr);assert not r.returncode,(r.returncode,r.stderr)
 for case in ['code','context']+(['unreadable'] if mode=='checked' else []):
  r=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=10);assert r.returncode==-6 and ('checked guest memory violation' if case=='unreadable' else 'unsupported RaiseException') in r.stderr,(r.returncode,r.stderr)
 runs.append(dict(mode=mode,passed=True,binary_sha256=sha(exe)));print(mode,'PASS',flush=True)
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=runs,source_sha256={str(p.relative_to(R)):sha(p) for p in sources+[R/'port/llasm-runtime/win32-bindings.json',pathlib.Path(__file__).resolve()]},scope='Native event/handle implementation and actual generated ABI. TSan instruments handles and test; full engine and platform acceptance are separate.'),indent=2)+'\n')
