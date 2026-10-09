#!/usr/bin/env python3
"""Validate host CPU-prefix contracts against ordinary checked original execution."""
import hashlib,json,os,pathlib,re,subprocess
from checked_codegen import command
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/cpu-host-checks';out.mkdir(parents=True,exist_ok=True)
tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
plugins=[R/'generated/tools'/n/(s+'.dylib') for n,s in [('guest-memory-pass','GuestMemoryPass'),('external-memory-pass','ExternalMemoryPass')]]
for p in plugins:
 d=json.loads(p.with_name('result.json').read_text());assert sha(p)==d['plugin_sha256'] and sha(R/'port/compiler'/(p.stem+'.cpp'))==d['source_sha256']
original=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe/engine-routed.ll';text=original.read_text();block=definition(text,'loc_428186')
lines=['target triple = "arm64-apple-macos12"','target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"',next(l for l in text.splitlines() if l.startswith('%_cpu = type')), '@route = global i32 0',block]
for i,n in enumerate(['428175','42818D']):lines+=['define hidden fastcc void @loc_'+n+'(%_cpu* %cpu) {store i32 '+str(i+1)+',ptr @route\nret void}']
lines+=['define hidden fastcc void @loc_1000(%_cpu* %cpu) {','%bp = getelementptr %_cpu, ptr %cpu, i32 0, i32 13','%base = load i64, ptr %bp','%op = getelementptr %_cpu, ptr %cpu, i32 0, i32 9','%offset = load i32, ptr %op','%wide = zext i32 %offset to i64','%address = add i64 %base, %wide','%guest = inttoptr i64 %address to ptr']
for i in range(8):lines+=['%%g%d = load i32,ptr %%guest'%i,'%%gn%d = add i32 %%g%d,1'%(i,i),'store i32 %%gn%d,ptr %%guest'%i,'%%p%d = getelementptr %%_cpu,ptr %%cpu,i32 0,i32 %d'%(i,i),'%%v%d = load i32,ptr %%p%d'%(i,i),'%%n%d = add i32 %%v%d,1'%(i,i),'store i32 %%n%d,ptr %%p%d'%(i,i)]
lines+=['ret void','}','define hidden fastcc void @loc_2000(ptr %cpu) {','%outside = getelementptr i8,ptr %cpu,i64 120','%v = load i32,ptr %outside','store i32 %v,ptr %cpu','ret void','}']
for n,target in [('cpu_batch','1000'),('cpu_block','428186'),('cpu_outside','2000')]:lines+=['define void @'+n+'(ptr %cpu) {call fastcc void @loc_'+target+'(ptr %cpu)\nret void}']
fixture=out/'input.ll';fixture.write_text('\n'.join(lines)+'\n')
policy=out/'policy.json';policy.write_text(json.dumps(dict(checked_symbols=[],host_only=[],range_checked=[],memory_manager_calls=[],memory_manager_sources=[],host_intrinsics=[],checked_strings=[],checked_callbacks=[],barrier_asm_sources=[],scalar_fcntl_commands=[])))
env=dict(os.environ,TRIBES_EXTERNAL_POLICY=str(policy));objects=[];census={}
for mode in ['baseline','optimized']:
 ir=out/(mode+'.ll');passes='tribes-guest-memory'+('-cpu-host' if mode=='optimized' else '')
 run=subprocess.run([str(tools/'opt'),'-load-pass-plugin',str(plugins[0]),'-passes='+passes+',verify','-S',str(fixture),'-o',str(ir)],capture_output=True,text=True);assert not run.returncode,run.stderr
 census[mode]=json.loads(next(l.split(' ',1)[1] for l in run.stderr.splitlines() if l.startswith('TRIBES_MEMORY_PASS ')))
 s=ir.read_text();f=definition(s,'loc_1000');outside=definition(s,'loc_2000')
 if mode=='optimized':
  assert f.count('call void @tribes_memory_access_host_span(')==1 and f.count('call i32 @tribes_memory_access_begin(')==16
  assert outside.count('call i32 @tribes_memory_access_begin(')==1 and outside.count('!tribes.checked.host.cpu')==1
  assert census[mode]['host_cpu_spans']==3 and census[mode]['host_cpu_accesses']>20
 else:assert '@tribes_memory_access_host_span' not in s and census[mode]['host_cpu_accesses']==0
 s=re.sub(r'@(loc_[0-9A-F]+|cpu_batch|cpu_block|cpu_outside|route)\b',lambda m:'@'+mode+'_'+m[1],s);ir.write_text(s)
 bc=out/(mode+'.bc');subprocess.run([str(tools/'opt'),'-load-pass-plugin',str(plugins[1]),'-passes=tribes-external-memory,verify',str(ir),'-o',str(bc)],env=env,check=True,capture_output=True)
 # Host fields may sit within a group, but do not extend its eight-access
 # limit or let a retained guest bracket cross a call/control-flow boundary.
 guarded=subprocess.check_output([str(tools/'llvm-dis'),str(bc),'-o','-'],text=True)
 pending=[];sums={};accesses=0;previous=''
 for line in guarded.splitlines():
  s=line.strip()
  if 'call i32 @tribes_memory_access_begin(' in s:
   token=s.split(' = ')[0];pending.append(token);sums[token]=[token]
  elif re.match(r'%tribes.end.count[^ ]* = add i32 ',s):
   key,args=s.split(' = add i32 ');left,right=args.split(', ');sums[key]=sums[left]+sums[right]
  elif 'call void @tribes_memory_access_end_many(' in s:
   assert sums[re.search(r'\(i32 ([^)]+)\)',s)[1]]==pending and 2<=len(pending)<=8
   pending=[];accesses=0
  elif 'call void @tribes_memory_access_end(' in s:
   token=re.search(r'\(i32 ([^)]+)\)',s)[1];assert pending==[token];pending=[];accesses=0
  elif re.search(r'\b(load|store)\b',s):
   if '!tribes.checked.host.cpu' not in s:assert '@tribes_memory_access_begin(' in previous,s
   if pending:accesses+=1;assert accesses<=8,s
  elif re.search(r'\b(call|br|ret|fence)\b',s):assert not pending,s
  previous=s
 assert not pending
 obj=out/(mode+'.o');subprocess.run(command(tools,'arm64-apple-macos12',bc,obj),check=True);objects.append(obj)
# Preexisting metadata must never be accepted as proof by either mode.
for mode in ['tribes-guest-memory','tribes-guest-memory-cpu-host']:
 bad=out/'forged.ll';bad.write_text(fixture.read_text().replace('%base = load i64, ptr %bp','%base = load i64, ptr %bp, !tribes.checked.host.cpu !0')+'\n!0 = !{}\n')
 run=subprocess.run([str(tools/'opt'),'-load-pass-plugin',str(plugins[0]),'-passes='+mode,str(bad),'-o',str(out/'forged.bc')],capture_output=True,text=True);assert run.returncode and 'Unverified CPU host metadata' in run.stderr
# Admission is deliberately narrow; malformed layout/signature and dynamic or
# crossing addresses retain ordinary checks instead of inheriting host proof.
prefix='\n'.join(lines[:3])+'\n'
variants={
 'visible':('define fastcc void @loc_3000(ptr %cpu)', '%p = getelementptr i8,ptr %cpu,i64 0',False),
 'calling-convention':('define hidden void @loc_3000(ptr %cpu)', '%p = getelementptr i8,ptr %cpu,i64 0',False),
 'name':('define hidden fastcc void @other(ptr %cpu)', '%p = getelementptr i8,ptr %cpu,i64 0',False),
 'dynamic':('define hidden fastcc void @loc_3000(ptr %cpu)', '%n = ptrtoint ptr %cpu to i64\n%p = getelementptr i8,ptr %cpu,i64 %n',False),
 'crossing':('define hidden fastcc void @loc_3000(ptr %cpu)', '%p = getelementptr i8,ptr %cpu,i64 118',False),
 'negative':('define hidden fastcc void @loc_3000(ptr %cpu)', '%p = getelementptr i8,ptr %cpu,i64 -4',False),
 'layout':('define hidden fastcc void @loc_3000(ptr %cpu)', '%p = getelementptr %_cpu,ptr %cpu,i32 0,i32 0',True),
}
for name,(signature,address,bad_layout) in variants.items():
 src=out/(name+'-input.ll');dst=out/(name+'-checked.ll')
 src.write_text((prefix.replace('[8 x double]','[7 x double]') if bad_layout else prefix)+signature+' {\n'+address+'\n%v = load i32,ptr %p\nret void\n}\n')
 run=subprocess.run([str(tools/'opt'),'-load-pass-plugin',str(plugins[0]),'-passes=tribes-guest-memory-cpu-host,verify','-S',str(src),'-o',str(dst)],capture_output=True,text=True);assert not run.returncode,run.stderr
 checked=dst.read_text();assert 'call void @tribes_memory_access_host_span' not in checked and checked.count('call i32 @tribes_memory_access_begin(')==1,name
results={};sources=[R/'tests/cpu_host_checks_test.c',R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
for tsan in [False,True]:
 exe=out/('test-tsan' if tsan else 'test')
 subprocess.run(['clang','-O2','-pthread','-Wall','-Wextra','-Werror']+(['-fsanitize=thread'] if tsan else [])+list(map(str,sources+objects))+['-o',str(exe)],check=True)
 run=subprocess.run([str(exe)]+(['concurrency-only'] if tsan else []),capture_output=True,text=True,timeout=60);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 results[exe.name]=dict(output=run.stdout.strip(),binary_sha256=sha(exe));print(run.stdout.strip())
(out/'result.json').write_text(json.dumps(dict(passed=True,census=census,results=results,original_block_sha256=hashlib.sha256(block.encode()).hexdigest(),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[R/'port/core/guest_memory_checks.h',R/'port/compiler/GuestMemoryPass.cpp',R/'port/compiler/ExternalMemoryPass.cpp',pathlib.Path(__file__).resolve()]},scope='Host prefix contract and preserved guest checks, one actual original block and controlled batch; not full-game or performance acceptance.'),indent=2)+'\n')
