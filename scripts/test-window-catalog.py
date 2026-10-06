#!/usr/bin/env python3
"""Window discovery through actual generated Win32 ABI; no renderer claim."""
import hashlib,json,pathlib,subprocess
from checked_support import compile_support
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/window-discovery';out.mkdir(parents=True,exist_ok=True)
sources=[R/'tests/window_catalog_test.c',R/'tests/fixtures/native_enter.ll',R/'port/core/guest_memory_checks.c',R/'port/llasm-support/tribes_absolute_memory.c'];flags=['-DPTROFS_64BIT','-Wno-override-module'];runs=[]
for mode in ['normal','checked']:
 build=R/'generated/native-win32'/('macos' if mode=='normal' else 'checked-macos');meta=json.loads((build/'result.json').read_text());assert mode!='checked' or meta['state']=='finished';objects=[]
 for name,row in meta['objects'].items():
  if name=='guest_dispatch':continue
  obj=build/(name+'.o');assert sha(obj)==row['object_sha256'];objects.append(str(obj))
 if mode=='checked':new,_=compile_support(sources,out/'checked',flags)
 else:new=list(map(str,sources))
 exe=out/('native-'+mode);cmd=['clang','-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip(),'-O2','-target','arm64-apple-macos12']+flags+new+objects+[str(R/'generated/softfloat/macos/softfloat.a'),'-framework','Foundation','-framework','AppKit','-o',str(exe)]
 r=subprocess.run(cmd,capture_output=True,text=True);(out/(mode+'-build.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/(mode+'-run.log')).write_text(r.stdout+r.stderr);assert not r.returncode,(r.returncode,r.stderr)
 faults=[]
 if mode=='checked':
  r=subprocess.run([str(exe),'readonly'],capture_output=True,text=True,timeout=30);assert r.returncode==-6 and 'checked guest memory violation' in r.stderr,(r.returncode,r.stderr);faults.append('readonly-process-id-output')
 runs.append(dict(mode=mode,passed=True,binary_sha256=sha(exe),runtime_build_sha256=sha(build/'result.json'),faults=faults));print(mode,'PASS',flush=True)
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=runs,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[R/'port/core/guest_window_catalog.c',R/'port/core/guest_window_catalog.h',pathlib.Path(__file__).resolve()]},scope='Original-width generated window API execution and catalog lifecycle/identity against explicit fixture windows. Native SDL linkage and original Miles traversal require full-engine verification.'),indent=2)+'\n')
