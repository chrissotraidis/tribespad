#!/usr/bin/env python3
"""Check cached lookup against all prepared mobile table addresses and aliases."""
import hashlib,json,pathlib,re,subprocess
from checked_codegen import command
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/dispatch-cache';out.mkdir(parents=True,exist_ok=True)
T=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
table=R/'generated/ipad-engine/mobile-inputs/merged-dispatch.ll'
text=table.read_text()
vas=list(map(int,re.findall(r'i32 (\d+)',re.search(r'@tribes_dispatch_vas = constant \[\d+ x i32\] \[(.*)\]',text)[1])))
names=re.findall(r'ptr @([^, ]+)',re.search(r'@tribes_dispatch_fns = constant \[\d+ x ptr\] \[(.*)\]',text)[1])
assert len(vas)==len(names) and vas==sorted(set(vas))
assert all(v not in vas for v in [0xfff12340,0xfff12344,0xfff12348,0xfff1234c])
indices={name:i for i,name in enumerate(sorted(set(names)))}
# Distinct real ARM64 RET entrypoints preserve the original table's complete
# alias mapping without linking or executing hundreds of thousands of game blocks.
assembly=out/'table.s'
with assembly.open('w') as f:
 f.write('.text\n.p2align 2\n_cache_fixture_targets:\n.rept %d\nret\n.endr\n'%len(indices))
 f.write('.section __TEXT,__const\n.p2align 2\n.globl _tribes_dispatch_count\n_tribes_dispatch_count:\n.long %d\n'%len(vas))
 f.write('.globl _tribes_dispatch_vas\n_tribes_dispatch_vas:\n')
 for v in vas:f.write('.long %d\n'%v)
 f.write('.section __DATA_CONST,__const\n.p2align 3\n.globl _tribes_dispatch_fns\n_tribes_dispatch_fns:\n')
 for name in names:f.write('.quad _cache_fixture_targets+%d\n'%(4*indices[name]))
fixture=out/'table.o';subprocess.run(['clang','-c',str(assembly),'-o',str(fixture)],check=True)
plugin=R/'generated/tools/guest-memory-pass/GuestMemoryPass.dylib'
meta=json.loads(plugin.with_name('result.json').read_text())
assert sha(plugin)==meta['plugin_sha256'] and sha(R/'port/compiler/GuestMemoryPass.cpp')==meta['source_sha256']
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
source=R/'port/core/guest_dispatch.c';runs=[]
for mode in ['baseline','cached']:
 raw=out/(mode+'-raw.ll');bc=out/(mode+'.bc');obj=out/(mode+'.o')
 subprocess.run([str(T/'clang'),'-isysroot',sdk,'-target','arm64-apple-macos12','-O2','-DPTROFS_64BIT']+(['-DTRIBESPAD_DISABLE_DISPATCH_CACHE'] if mode=='baseline' else [])+['-S','-emit-llvm',str(source),'-o',str(raw)],check=True)
 p=subprocess.run([str(T/'opt'),'-load-pass-plugin',str(plugin),'-passes=tribes-guest-memory-cpu-host,verify',str(raw),'-o',str(bc)],capture_output=True,text=True)
 assert not p.returncode,p.stderr
 (out/(mode+'-pass.log')).write_text(p.stderr)
 subprocess.run(command(T,'arm64-apple-macos12',bc,obj),check=True)
 for tsan in [False,True]:
  exe=out/(mode+('-tsan' if tsan else ''))
  # Instrument the actual dispatcher IR too, so TSan observes cache accesses.
  dispatch=obj
  if tsan:
   dispatch=out/(mode+'-tsan.o')
   subprocess.run(['clang','-isysroot',sdk,'-target','arm64-apple-macos12','-O0','-fsanitize=thread','-c',str(bc),'-o',str(dispatch)],check=True)
  subprocess.run(['clang','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-pthread']+(['-fsanitize=thread'] if tsan else [])+[str(R/'tests/dispatch_cache_test.c'),str(R/'port/core/guest_memory_checks.c'),str(dispatch),str(fixture),'-o',str(exe)],check=True)
  run=subprocess.run([str(exe)]+(['threads'] if tsan else []),capture_output=True,text=True,timeout=120)
  (out/(exe.name+'.log')).write_text(run.stdout+run.stderr)
  assert run.returncode==0,(run.returncode,run.stdout,run.stderr)
  assert run.stdout.startswith('PASS:')
  if tsan:assert not run.stderr,run.stderr
  else:assert run.stderr.count('unpublished original guest PC')==1 and run.stderr.count('unsupported original guest PC')==1,run.stderr
  print(mode,tsan,run.stdout.strip(),flush=True)
  runs.append(dict(mode=mode,tsan=tsan,output=run.stdout.strip(),hot_seconds=float(re.search(r'hot=([\d.]+)',run.stdout)[1]),cold_seconds=float(re.search(r'cold=([\d.]+)',run.stdout)[1]),binary_sha256=sha(exe)))
paths=[source,R/'tests/dispatch_cache_test.c',R/'port/core/guest_memory_checks.c',pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,table_sha256=sha(table),entries=len(vas),unique_targets=len(indices),runs=runs,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Real prepared address/alias table with distinct ARM64 RET fixtures; checked lookup comparison, cached publication denial and native provider changes. No original gameplay or FPS claim.'),indent=2)+'\n')
