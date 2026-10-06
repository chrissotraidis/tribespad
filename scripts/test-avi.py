#!/usr/bin/env python3
"""Verify the reached AVI lifetime calls through the emitted original ABI."""
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/avi-lifetime';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({n:bindings[n] for n in ['AVIFileInit','AVIFileExit']},False))
sources=[R/n for n in ['port/core/guest_avi.c','tests/fixtures/avi_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
objects,checked=compile_support(sources,out/'checked',['-DPTROFS_64BIT']);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded');sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();runs=[]
host=[R/n for n in ['tests/avi_test.c','port/core/guest_memory_checks.c','port/core/guest_external_checks.c']]
for mode in ['normal','checked','tsan']:
 exe=out/('test-'+mode);cmd=['clang','-isysroot',sdk,'-target','arm64-apple-macos12','-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*map(str,host)]+(['-DCHECKED']+objects if mode=='checked' else list(map(str,sources)))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);assert r.returncode==0 and 'PASS:' in r.stdout and not r.stderr,(mode,r.stdout,r.stderr)
 bad=subprocess.run([str(exe),'underflow'],capture_output=True,text=True,timeout=30);assert bad.returncode==-6 and 'unbalanced AVI library lifetime' in bad.stderr
 runs.append(dict(mode=mode,passed=True,stdout=r.stdout,binary_sha256=sha(exe)));print(mode,r.stdout.strip())
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=runs,checked_sha256=checked,guarded_sha256=guarded,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+host+[R/'port/core/guest_avi.h',pathlib.Path(__file__).resolve()]}),indent=2)+'\n')
