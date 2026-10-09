#!/usr/bin/env python3
"""Validate the finite native CPU contract and dominance-based index bounds."""
import hashlib,json,os,pathlib,subprocess
from checked_codegen import command
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/native-cpu-checks';out.mkdir(parents=True,exist_ok=True)
T=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
plugins=[R/'generated/tools'/n/(c+'.dylib') for n,c in [('guest-memory-pass','GuestMemoryPass'),('external-memory-pass','ExternalMemoryPass')]]
for p in plugins:
 d=json.loads(p.with_name('result.json').read_text());assert sha(p)==d['plugin_sha256'] and sha(R/'port/compiler'/(p.stem+'.cpp'))==d['source_sha256']
def module(functions):
 names=list(functions)
 annotations=', '.join('{ptr,ptr,ptr,i32,ptr} {ptr @%s,ptr @label,ptr null,i32 1,ptr null}'%n for n in names)
 return '''target triple = "arm64-apple-macos12"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"
@label = private constant [18 x i8] c"tribes.native-cpu\\00"
@llvm.global.annotations = appending global [%d x {ptr,ptr,ptr,i32,ptr}] [%s], section "llvm.metadata"
declare void @abort()
'''%(len(names),annotations)+'\n'.join(functions.values())
functions={
 'native_probe':'''define i32 @native_probe(ptr %cpu,ptr %guest,i64 %index) {
 %ok = icmp ult i64 %index,8
 br i1 %ok,label %body,label %bad
bad:
 call void @abort()
 unreachable
body:
 %base = getelementptr i8,ptr %cpu,i64 176
 %reg = getelementptr [16 x i8],ptr %base,i64 %index
 %v = load i32,ptr %guest
 store i32 %v,ptr %reg
 %r = load i32,ptr %reg
 %n = add i32 %r,1
 store i32 %n,ptr %guest
 ret i32 %r
}''',
 'native_outside':'''define void @native_outside(ptr %cpu) {
 %v = load i32,ptr %cpu
 %p = getelementptr i8,ptr %cpu,i64 440
 store i32 %v,ptr %p
 ret void
}''',
 'native_unbounded':'''define void @native_unbounded(ptr %cpu,i64 %offset) {
 %v = load i32,ptr %cpu
 %p = getelementptr i8,ptr %cpu,i64 %offset
 store i32 %v,ptr %p
 ret void
}''',
 'native_pointee':'''define void @native_pointee(ptr %cpu) {
 %p = getelementptr i8,ptr %cpu,i64 112
 %v = load ptr,ptr %p
 store i32 19,ptr %v
 ret void
}'''}
def check(name,text,optimized=True):
 src=out/(name+'-input.ll');dst=out/(name+'.ll');src.write_text(text)
 run=subprocess.run([str(T/'opt'),'-load-pass-plugin',str(plugins[0]),'-passes=tribes-guest-memory'+('-cpu-host' if optimized else '')+',verify','-S',str(src),'-o',str(dst)],capture_output=True,text=True)
 assert run.returncode==0,run.stderr
 return dst,json.loads(next(l.split(' ',1)[1] for l in run.stderr.splitlines() if l.startswith('TRIBES_MEMORY_PASS ')))
ir,census=check('runtime',module(functions));assert census['host_cpu_accesses']==5 and census['host_cpu_spans']==4 and census['loads']==1 and census['stores']==4,census
_,baseline=check('baseline',module(functions),False);assert baseline['host_cpu_accesses']==0 and baseline['loads']+baseline['stores']==10
variants={
 'first':('','%p = getelementptr i8,ptr %cpu,i64 0','i8',True),
 'last':('','%p = getelementptr i8,ptr %cpu,i64 439','i8',True),
 'crossing':('','%p = getelementptr i8,ptr %cpu,i64 439','i16',False),
 'negative':('','%p = getelementptr i8,ptr %cpu,i64 -1','i8',False),
 'unbounded':('','%p = getelementptr i8,ptr %cpu,i64 %index','i8',False),
 'guarded':('%ok = icmp ult i64 %index,8\nbr i1 %ok,label %body,label %bad\nbad:\nret void\nbody:', '%base = getelementptr i8,ptr %cpu,i64 312\n%p = getelementptr [16 x i8],ptr %base,i64 %index','i128',True),
 'guard-too-wide':('%ok = icmp ult i64 %index,9\nbr i1 %ok,label %body,label %bad\nbad:\nret void\nbody:', '%base = getelementptr i8,ptr %cpu,i64 312\n%p = getelementptr [16 x i8],ptr %base,i64 %index','i128',False),
 'signed-negative':('%ok = icmp slt i64 %index,8\nbr i1 %ok,label %body,label %bad\nbad:\nret void\nbody:', '%p = getelementptr i8,ptr %cpu,i64 %index','i8',False),
 'volatile':('','%p = getelementptr i8,ptr %cpu,i64 0','volatile i32',False),
 'loaded-pointer':('','%p = load ptr,ptr %cpu','i32',False),
}
for name,(guard,address,ty,admit) in variants.items():
 _,c=check(name,module({'probe':'define void @probe(ptr %cpu,i64 %index) {\n'+guard+'\n'+address+'\n%v = load '+ty+',ptr %p\nret void\n}'}))
 expected=int(admit)+(1 if name=='loaded-pointer' else 0)
 assert c['host_cpu_accesses']==expected,(name,c)
 assert c['loads']==int(not admit),(name,c)
policy=out/'policy.json';policy.write_text(json.dumps(dict(checked_symbols=[],host_only=['abort'],range_checked=[],memory_manager_calls=[],memory_manager_sources=[],host_intrinsics=[],checked_strings=[],checked_callbacks=[],barrier_asm_sources=[],scalar_fcntl_commands=[])))
bc=out/'guarded.bc';env=dict(os.environ,TRIBES_EXTERNAL_POLICY=str(policy))
subprocess.run([str(T/'opt'),'-load-pass-plugin',str(plugins[1]),'-passes=tribes-external-memory,verify',str(ir),'-o',str(bc)],env=env,check=True,capture_output=True)
obj=out/'guarded.o';subprocess.run(command(T,'arm64-apple-macos12',bc,obj),check=True)
sources=[R/'tests/native_cpu_checks_test.c',R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];runs=[]
for tsan in [False,True]:
 exe=out/('tsan' if tsan else 'normal');subprocess.run(['clang','-O2','-pthread','-Wall','-Wextra','-Werror']+(['-fsanitize=thread'] if tsan else [])+list(map(str,sources))+[str(obj),'-o',str(exe)],check=True)
 run=subprocess.run([str(exe)]+(['concurrency-only'] if tsan else []),capture_output=True,text=True,timeout=60);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 print(run.stdout.strip());runs.append(dict(mode=exe.name,output=run.stdout.strip(),binary_sha256=sha(exe)))
paths=sources+[R/'port/compiler/GuestMemoryPass.cpp',R/'port/compiler/ExternalMemoryPass.cpp',R/'port/core/guest_memory_checks.h',R/'port/llasm-support/llasm_cpu.h',R/'port/llasm-support/tribes_sse.c',pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,census=census,baseline=baseline,variants=list(variants),runs=runs,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Finite 440-byte native CPU proof, bounded indexed fields, preserved guest pointees and concurrent independent CPU objects. No full-game performance claim.'),indent=2)+'\n')
