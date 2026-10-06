#!/usr/bin/env python3
"""Execute LLVM-pass-emitted memory barriers; no game coverage claim."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/guest-memory-pass';out.mkdir(parents=True,exist_ok=True);report=out/'result.json';report.write_text('{"state":"running","pass":false}\n')
tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');plugin=R/'generated/tools/guest-memory-pass/GuestMemoryPass.dylib';build=json.loads(plugin.with_name('result.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();assert build['plugin_sha256']==sha(plugin) and build['source_sha256']==sha(R/'port/compiler/GuestMemoryPass.cpp')
def transform(src,dest):return subprocess.run([str(tools/'opt'),'-load-pass-plugin',str(plugin),'-passes=tribes-guest-memory,verify','-S',str(src),'-o',str(dest)],capture_output=True,text=True)
fixture=R/'tests/fixtures/guest_memory.ll';ir=out/'checked.ll';run=transform(fixture,ir);assert run.returncode==0,run.stderr;(out/'pass.log').write_text(run.stderr)
census=json.loads(next(line.split(' ',1)[1] for line in run.stderr.splitlines() if line.startswith('TRIBES_MEMORY_PASS ')));assert {k:census[k] for k in ['loads','stores','atomics','transfers','sets']}==dict(loads=2,stores=2,atomics=2,transfers=5,sets=3),census
# Previously pure function attributes cannot hide the synchronization/checks.
text=ir.read_text();assert 'tail call ptr @mem' not in text
for line in text.splitlines():
 if line.startswith('define ') and ('@checked_read(' in line or '@checked_write(' in line):assert '#' not in line and 'memory(' not in line and 'nosync' not in line,line
cases={'duplicate':ir,'scalable':out/'scalable.ll','masked':out/'masked.ll','vaarg':out/'vaarg.ll'}
cases['scalable'].write_text('define <vscale x 4 x i32> @f(ptr %p) { %v = load <vscale x 4 x i32>, ptr %p\n ret <vscale x 4 x i32> %v }\n')
cases['masked'].write_text('declare <4 x i32> @llvm.masked.load.v4i32.p0(ptr,i32 immarg,<4 x i1>,<4 x i32>)\ndefine <4 x i32> @f(ptr %p,<4 x i1> %m) { %v = call <4 x i32> @llvm.masked.load.v4i32.p0(ptr %p,i32 4,<4 x i1> %m,<4 x i32> zeroinitializer)\n ret <4 x i32> %v }\n')
cases['vaarg'].write_text('define i32 @f(ptr %p) { %v = va_arg ptr %p, i32\n ret i32 %v }\n')
for name,src in cases.items():
 run=transform(src,out/(name+'-rejected.ll'));(out/(name+'-reject.log')).write_text(run.stderr);assert run.returncode!=0 and ('LLVM ERROR:' in run.stderr),name
obj=out/'checked.o';subprocess.run([str(tools/'clang'),'-target','arm64-apple-macos12','-O0','-Wno-override-module','-c',str(ir),'-o',str(obj)],check=True)
sources=[R/'port/core/guest_memory_checks.c',R/'tests/guest_memory_checks_test.c'];exe=out/'test';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-pthread','-Wall','-Wextra','-Werror']+list(map(str,sources))+[str(obj),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=45);(out/'run.log').write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
tsan=out/'test-tsan';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-pthread','-fsanitize=thread','-Wall','-Wextra','-Werror']+list(map(str,sources))+[str(obj),'-o',str(tsan)],check=True)
tsan_run=subprocess.run([str(tsan),'concurrency-only'],capture_output=True,text=True,timeout=45);(out/'tsan.log').write_text(tsan_run.stdout+tsan_run.stderr);assert not tsan_run.returncode and tsan_run.stdout.startswith('PASS:') and not tsan_run.stderr,(tsan_run.returncode,tsan_run.stdout,tsan_run.stderr)
report.write_text(json.dumps({'tsan_output':tsan_run.stdout.strip(),'tsan_binary_sha256':sha(tsan),'state':'finished','pass':True,'output':run.stdout.strip(),'census':census,'rejections':list(cases),'binary_sha256':sha(exe),'checked_ir_sha256':sha(ir),'checked_object_sha256':sha(obj),'plugin_build_sha256':sha(plugin.with_name('result.json')),'source_sha256':{str(p.relative_to(R)):sha(p) for p in sources+[fixture,R/'port/core/guest_memory_checks.h',pathlib.Path(__file__).resolve()]},'scope':'Compiler/runtime foundation only. Native DLL/services/library coverage and VirtualProtect integration remain unproven; not enabled in game bootstrap.'},indent=2)+'\n');print(run.stdout.strip())
