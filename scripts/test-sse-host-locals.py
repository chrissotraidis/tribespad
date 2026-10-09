#!/usr/bin/env python3
"""Replay the existing original-instruction oracle through both checked SSE builds."""
import hashlib,json,pathlib,subprocess,time
from checked_support import compile_support
from checked_codegen import command
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sse-host-locals';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();T=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');m=sse_inputs('macos')
fixture=R/'generated/sse/lowering-scalar/macos';proof=json.loads((fixture/'result.json').read_text());assert proof['pass'] and proof['scalar_enabled'] and proof['cases']==94128
for name,key in [('cases.inc','corpus_sha256'),('blocks.ll','llvm_sha256')]:assert sha(fixture/name)==proof[key]
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
flags=['-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-Wno-override-module','-DPTROFS_64BIT',*m['compile_flags'],'-I',str(fixture)]
# Reuse the exact differential fixture: complete CPU/vector/flags/stack state,
# memory canaries, original scalar instruction bodies and independent threads.
objects=[]
for p in [fixture/'blocks.ll',R/'tests/sse_lowering_test.c']:
 obj=out/(p.stem+'.o');subprocess.run([str(T/'clang'),*flags,'-c',str(p),'-o',str(obj)],check=True);objects.append(obj)
sources=[R/'port/llasm-support/tribes_sse.c',R/'port/core/sse_arithmetic.c'];runs=[];census={}
for mode in ['baseline','optimized']:
 bitcodes,_=compile_support(sources,out/mode,['-DPTROFS_64BIT',*m['compile_flags']],defer_codegen=True,cpu_host=mode=='optimized')
 meta=json.loads((out/mode/'result.json').read_text());census[mode]={name:row['census'] for name,row in meta['objects'].items()};native=[]
 for p in map(pathlib.Path,bitcodes):
  obj=p.with_suffix('.o');subprocess.run(command(T,'arm64-apple-macos12',p,obj),check=True);native.append(obj)
 for tsan in [False,True]:
  exe=out/(mode+('-tsan' if tsan else '-normal'))
  subprocess.run(['clang',*flags,*(['-fsanitize=thread'] if tsan else []),str(R/'port/core/guest_memory_checks.c'),*map(str,objects+native),*m['libraries'],'-o',str(exe)],check=True)
  start=time.monotonic();run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120);elapsed=time.monotonic()-start
  assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
  print(mode,tsan,run.stdout.strip(),flush=True);runs.append(dict(mode=mode,tsan_guard_runtime=tsan,output=run.stdout.strip(),seconds=elapsed,binary_sha256=sha(exe)))
paths=sources+[R/'port/llasm-support/llasm_cpu.h',R/'port/llasm-support/tribes_sse.h',R/'port/core/sse_arithmetic.h',R/'tests/sse_lowering_test.c',R/'port/core/guest_memory_checks.c',R/'port/compiler/GuestMemoryPass.cpp',R/'scripts/checked_support.py',pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,cases=proof['cases'],corpus_sha256=proof['corpus_sha256'],original_blocks_sha256=proof['llvm_sha256'],census=census,runs=runs,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Same original AOT scalar cases replayed through baseline and native-object optimized checked SSE helpers; TSan covers guard runtime. Timings include startup and threads and are not game FPS.'),indent=2)+'\n')
