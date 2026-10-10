#!/usr/bin/env python3
"""Prove bounded native-object access elision while keeping guest pointees checked."""
import hashlib,json,os,pathlib,re,subprocess
from checked_codegen import command
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/host-local-checks';out.mkdir(parents=True,exist_ok=True)
T=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
plugins=[R/'generated/tools'/n/(c+'.dylib') for n,c in [('guest-memory-pass','GuestMemoryPass'),('external-memory-pass','ExternalMemoryPass')]]
for p in plugins:
 d=json.loads(p.with_name('result.json').read_text());assert sha(p)==d['plugin_sha256'] and sha(R/'port/compiler'/(p.stem+'.cpp'))==d['source_sha256']
prefix='''target triple = "arm64-apple-macos12"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"
@constant = private constant i32 305419896
@external_tls = external thread_local global i32
@pointer = internal thread_local global ptr null
@masked_slots = internal thread_local global [256 x i32] zeroinitializer
@imported = external global [8 x i8]
@local = private global [8 x i8] zeroinitializer
@tls_bytes = external thread_local global [8 x i8]
declare ptr @memcpy(ptr,ptr,i64)
declare void @llvm.memcpy.p0.p0.i64(ptr,ptr,i64,i1)
declare void @llvm.memset.p0.i64(ptr,i8,i64,i1)
declare ptr @llvm.threadlocal.address.p0(ptr)
'''
bulk_body='''define i32 @host_bulk(ptr %guest) {
 %src = alloca [8 x i8]
 %dst = alloca [8 x i8]
 call void @llvm.memset.p0.i64(ptr %src,i8 37,i64 8,i1 false)
 %r = call ptr @memcpy(ptr %dst,ptr %src,i64 8)
 call void @llvm.memcpy.p0.p0.i64(ptr %guest,ptr %dst,i64 8,i1 false)
 %v = load i32,ptr %dst
 ret i32 %v
}
'''
body='''define i32 @host_locals(ptr %guest,i32 %n) {
 %slot = alloca i32
 store i32 %n,ptr %slot
 %v = load i32,ptr %slot
 %tls = call ptr @llvm.threadlocal.address.p0(ptr @external_tls)
 store i32 %v,ptr %tls
 %r = load i32,ptr %tls
 %k = load i32,ptr @constant
 %g = load i32,ptr %guest
 %inc = add i32 %g,1
 store i32 %inc,ptr %guest
 %ret = xor i32 %r,%k
 ret i32 %ret
}
define void @host_pointee(ptr %guest,i32 %n) {
 %tls = call ptr @llvm.threadlocal.address.p0(ptr @pointer)
 store ptr %guest,ptr %tls
 %p = load ptr,ptr %tls
 store i32 %n,ptr %p
 ret void
}
'''
body+=bulk_body
body+='''define i32 @host_masked(i64 %n,i32 %value) {
 %a = call ptr @llvm.threadlocal.address.p0(ptr @masked_slots)
 %i = and i64 %n,255
 %p = getelementptr [256 x i32],ptr %a,i64 0,i64 %i
 store i32 %value,ptr %p
 %r = load i32,ptr %p
 ret i32 %r
}
'''
policy=out/'policy.json';policy.write_text(json.dumps(dict(checked_symbols=[],host_only=[],range_checked=['memcpy'],memory_manager_calls=[],memory_manager_sources=[],host_intrinsics=[],checked_strings=[],checked_callbacks=[],barrier_asm_sources=[],scalar_fcntl_commands=[])))
def check(name,text,optimized=True):
 src=out/(name+'-input.ll');dst=out/(name+'.ll');src.write_text(text)
 run=subprocess.run([str(T/'opt'),'-load-pass-plugin',str(plugins[0]),'-passes=tribes-guest-memory'+('-cpu-host' if optimized else '')+',verify','-S',str(src),'-o',str(dst)],capture_output=True,text=True)
 assert run.returncode==0,run.stderr
 return dst,json.loads(next(l.split(' ',1)[1] for l in run.stderr.splitlines() if l.startswith('TRIBES_MEMORY_PASS ')))
ir,census=check('runtime',prefix+body);assert census['host_bulk_operations']==2 and census['host_local_accesses']==10 and census['loads']==1 and census['stores']==2,census
_,baseline=check('baseline',prefix+body,False);assert baseline['host_local_accesses']==0 and baseline['loads']+baseline['stores']==13,baseline
# Include the first/last valid byte, crossing and negative ranges, dynamic
# offsets/allocation extents, volatility, atomics and ordinary imported globals.
variants={
 'stack-first':('%a = alloca [8 x i8]\n%p = getelementptr i8,ptr %a,i64 0','load i8',True),
 'stack-last':('%a = alloca [8 x i8]\n%p = getelementptr i8,ptr %a,i64 7','load i8',True),
 'stack-cross':('%a = alloca [8 x i8]\n%p = getelementptr i8,ptr %a,i64 7','load i16',False),
 'stack-negative':('%a = alloca [8 x i8]\n%p = getelementptr i8,ptr %a,i64 -1','load i8',False),
 'stack-dynamic-offset':('%a = alloca [8 x i8]\n%p = getelementptr i8,ptr %a,i64 %offset','load i8',False),
 'stack-masked':('%a = alloca [8 x i8]\n%i = and i64 %offset,7\n%p = getelementptr i8,ptr %a,i64 %i','load i8',True),
 'stack-masked-cross':('%a = alloca [8 x i8]\n%i = and i64 %offset,7\n%p = getelementptr i8,ptr %a,i64 %i','load i16',False),
 'stack-masked-scaled':('%a = alloca [8 x i32]\n%i = and i64 %offset,7\n%p = getelementptr i32,ptr %a,i64 %i','load i32',True),
 'stack-masked-negative-scale':('%a = alloca [8 x i8]\n%i = and i64 %offset,7\n%neg = sub i64 0,%i\n%p = getelementptr i8,ptr %a,i64 %neg','load i8',False),
 'stack-masked-overflow':('%a = alloca [8 x i8]\n%i = and i64 %offset,9223372036854775807\n%p = getelementptr i64,ptr %a,i64 %i','load i8',False),
 'tls-masked':('%a = call ptr @llvm.threadlocal.address.p0(ptr @tls_bytes)\n%i = and i64 %offset,7\n%p = getelementptr i8,ptr %a,i64 %i','load i8',True),
 'global-masked':('%i = and i64 %offset,7\n%p = getelementptr i8,ptr @local,i64 %i','load i8',True),
 'imported-masked':('%i = and i64 %offset,7\n%p = getelementptr i8,ptr @imported,i64 %i','load i8',False),
 'stack-dynamic-size':('%p = alloca i8,i64 %offset','load i8',False),
 'global-last':('%p = getelementptr i8,ptr @local,i64 7','load i8',True),
 'global-cross':('%p = getelementptr i8,ptr @local,i64 7','load i16',False),
 'imported':('%p = getelementptr i8,ptr @imported,i64 0','load i8',False),
 'tls-last':('%a = call ptr @llvm.threadlocal.address.p0(ptr @tls_bytes)\n%p = getelementptr i8,ptr %a,i64 7','load i8',True),
 'tls-cross':('%a = call ptr @llvm.threadlocal.address.p0(ptr @tls_bytes)\n%p = getelementptr i8,ptr %a,i64 7','load i16',False),
 'tls-direct':('%p = getelementptr i8,ptr @tls_bytes,i64 0','load i8',True),
 'volatile':('%p = alloca i32','load volatile i32',False),
 'atomic':('%p = alloca i32','load atomic i32',False),
}
for name,(address,load,admit) in variants.items():
 tail=' monotonic,align 4' if name=='atomic' else ''
 _,c=check(name,prefix+'define void @probe(i64 %offset) {\n'+address+'\n%v = '+load+',ptr %p'+tail+'\nret void\n}\n')
 assert c['host_local_accesses']==int(admit) and c['loads']==int(not admit),(name,c)
# Both endpoints must be wholly native; crossing, loaded and guest pointers
# cannot inherit a native object's proof. Volatile and unknown lengths remain.
for name,address,length,volatile,admit in [
 ('bulk-local','%p = getelementptr i8,ptr %slot,i64 0','8','false',True),
 ('bulk-cross','%p = getelementptr i8,ptr %slot,i64 1','8','false',False),
 ('bulk-negative','%p = getelementptr i8,ptr %slot,i64 -1','8','false',False),
 ('bulk-guest','%p = getelementptr i8,ptr %guest,i64 0','8','false',False),
 ('bulk-dynamic','%p = getelementptr i8,ptr %slot,i64 0','%length','false',False),
 ('bulk-volatile','%p = getelementptr i8,ptr %slot,i64 0','8','true',False),
]:
 text=prefix+'define void @probe(ptr %guest,i64 %length) {\n%slot = alloca [8 x i8]\n%dst = alloca [8 x i8]\n'+address+'\ncall void @llvm.memcpy.p0.p0.i64(ptr %dst,ptr %p,i64 '+length+',i1 '+volatile+')\nret void\n}\n'
 _,c=check(name,text);assert c['host_bulk_operations']==int(admit) and c['transfers']==int(not admit),(name,c)
for optimized in [False,True]:
 src=out/'forged-bulk.ll';src.write_text((prefix+bulk_body).replace('i64 8,i1 false)','i64 8,i1 false), !tribes.checked.host.bulk !0')+'\n!0 = !{}\n')
 run=subprocess.run([str(T/'opt'),'-load-pass-plugin',str(plugins[0]),'-passes=tribes-guest-memory'+('-cpu-host' if optimized else ''),str(src),'-o',str(out/'forged-bulk.bc')],capture_output=True,text=True)
 assert run.returncode and 'Unverified bulk host metadata' in run.stderr,run.stderr
for mode in ['tribes-guest-memory','tribes-guest-memory-cpu-host']:
 src=out/'forged.ll';src.write_text((prefix+body).replace('%v = load i32,ptr %slot','%v = load i32,ptr %slot, !tribes.checked.host.local !0')+'\n!0 = !{}\n')
 run=subprocess.run([str(T/'opt'),'-load-pass-plugin',str(plugins[0]),'-passes='+mode,str(src),'-o',str(out/'forged.bc')],capture_output=True,text=True);assert run.returncode and 'Unverified local host metadata' in run.stderr
bc=out/'guarded.bc';env=dict(os.environ,TRIBES_EXTERNAL_POLICY=str(policy))
subprocess.run([str(T/'opt'),'-load-pass-plugin',str(plugins[1]),'-passes=tribes-external-memory,verify',str(ir),'-o',str(bc)],env=env,check=True,capture_output=True)
obj=out/'guarded.o';subprocess.run(command(T,'arm64-apple-macos12',bc,obj),check=True)
# Proven native accesses must still count toward the existing sixteen-access
# retirement limit when interleaved with retained guest brackets.
group='define void @group(ptr %guest) {\n%slot = alloca i32\n'
for i in range(16):group+='%%g%d = load i32,ptr %%guest\nstore i32 %%g%d,ptr %%slot\n'%(i,i)
group+='ret void\n}\n'
group_ir,_=check('groups',prefix+group)
guarded=out/'groups-guarded.ll'
subprocess.run([str(T/'opt'),'-load-pass-plugin',str(plugins[1]),'-passes=tribes-external-memory,verify','-S',str(group_ir),'-o',str(guarded)],env=env,check=True,capture_output=True)
pending=[];sums={};accesses=0;maximum=0
for line in guarded.read_text().splitlines():
 s=line.strip()
 if 'call i32 @tribes_memory_access_begin(' in s:
  token=s.split(' = ')[0];pending.append(token);sums[token]=[token]
 elif re.match(r'%tribes.end.count[^ ]* = add i32 ',s):
  key,args=s.split(' = add i32 ');left,right=args.split(', ');sums[key]=sums[left]+sums[right]
 elif 'call void @tribes_memory_access_end_many(' in s:
  assert sums[re.search(r'\(i32 ([^)]+)\)',s)[1]]==pending and accesses<=16
  maximum=max(maximum,accesses);pending=[];accesses=0
 elif 'call void @tribes_memory_access_end(' in s:
  assert pending==[re.search(r'\(i32 ([^)]+)\)',s)[1]];pending=[];accesses=0
 elif re.search(r'\b(load|store)\b',s):
  if pending:accesses+=1;assert accesses<=16
 elif re.search(r'\b(call|br|ret|fence)\b',s):assert not pending
assert not pending and maximum==16
sources=[R/'tests/host_local_checks_test.c',R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];runs=[]
for tsan in [False,True]:
 exe=out/('tsan' if tsan else 'normal');subprocess.run(['clang','-O2','-pthread','-Wall','-Wextra','-Werror']+(['-fsanitize=thread'] if tsan else [])+list(map(str,sources))+[str(obj),'-o',str(exe)],check=True)
 run=subprocess.run([str(exe)]+(['concurrency-only'] if tsan else []),capture_output=True,text=True,timeout=60);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);print(run.stdout.strip());runs.append(dict(mode=exe.name,output=run.stdout.strip(),binary_sha256=sha(exe)))
paths=sources+[R/'port/compiler/GuestMemoryPass.cpp',R/'port/compiler/ExternalMemoryPass.cpp',R/'scripts/checked_support.py',pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,census=census,baseline=baseline,variants=list(variants),runs=runs,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Bounded compiler-owned native stack, TLS and local global accesses; guest pointees still checked. No gameplay performance claim.'),indent=2)+'\n')
