#!/usr/bin/env python3
"""Replay existing x87 numerical oracles through both checked CPU policies."""
import hashlib,json,pathlib,subprocess,sys
from checked_support import compile_support
from checked_codegen import command
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/x87-host-checks';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
T=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');math=sse_inputs('macos')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
flags=['-isysroot',sdk,'-target','arm64-apple-macos12','-O2','-DPTROFS_64BIT',*math['compile_flags']]
sources=[R/p for p in ['port/llasm-support/tribes_float80.c','port/llasm-support/tribes_mmx.c','port/core/mmx.c','tests/x87_result_slot.c']]
bochs=R/'generated/bochs-math/libmath.dylib';bm=json.loads(bochs.with_name('result.json').read_text())
assert sha(bochs)==bm['native_bridge_sha256'] and sha(R/'port/bochs-support/math_bridge.cc')==bm['bridge_sha256']
runs=[];census={}
for mode in ['baseline','optimized']:
 work=out/mode;work.mkdir(exist_ok=True)
 bitcodes,_=compile_support(sources,work/'checked',flags,defer_codegen=True,cpu_host=mode=='optimized')
 meta=json.loads((work/'checked/result.json').read_text());census[mode]={n:r['census'] for n,r in meta['objects'].items()}
 objects=[]
 for name in bitcodes:
  p=pathlib.Path(name);obj=p.with_suffix('.o');subprocess.run(command(T,'arm64-apple-macos12',p,obj),check=True);objects.append(obj)
 library=work/'libx87.dylib'
 subprocess.run(['clang',*flags,'-dynamiclib',*map(str,objects),str(R/'port/core/guest_memory_checks.c'),*math['libraries'],str(bochs),'-o',str(library)],check=True)
 numerics={}
 for name in ['runtime','compare','stores']:
  dest=work/name
  run=subprocess.run([sys.executable,str(R/('scripts/test-float80-'+name+'.py')),'--library',str(library),'--output',str(dest)],capture_output=True,text=True)
  (work/(name+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0,(name,run.stdout,run.stderr)
  result=json.loads((dest/'result.json').read_text());assert result.get('pass',result.get('passed')) and result['library_sha256']==sha(library)
  numerics[name]={'cases':result['cases'],'passed':True};print(mode,name,result['cases'],flush=True)
 exe=work/'guard-test'
 subprocess.run(['clang',*flags,'-Wall','-Wextra','-Werror',str(R/'tests/x87_host_checks_test.c'),str(library),'-o',str(exe)],check=True)
 run=subprocess.run([str(exe),mode],capture_output=True,text=True,timeout=60)
 (work/'guard.log').write_text(run.stdout+run.stderr);assert run.returncode==0,(run.stdout,run.stderr)
 print(mode,run.stdout.strip(),flush=True)
 runs.append(dict(mode=mode,library_sha256=sha(library),numerics=numerics,guards=run.stdout.strip(),guard_binary_sha256=sha(exe)))
paths=sources+[R/'port/llasm-support/llasm_cpu.h',R/'port/core/guest_memory_checks.c',R/'port/compiler/GuestMemoryPass.cpp',R/'tests/x87_host_checks_test.c',R/'scripts/checked_support.py',pathlib.Path(__file__).resolve()]+[R/('scripts/test-float80-'+n+'.py') for n in ['runtime','compare','stores']]
(out/'result.json').write_text(json.dumps(dict(passed=True,census=census,runs=runs,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Unchanged numerical reference cases through baseline and CPU-object-optimized checked x87 wrappers; raw x86 reference runs under Rosetta. Guest tword pointees remain checked; finite CPU overlap and invalid stack indices fail. Eight independent CPU threads. Math dependency is the pinned native reference, not the full engine link. No game FPS claim.'),indent=2)+'\n')
