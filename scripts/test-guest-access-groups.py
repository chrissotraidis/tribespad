#!/usr/bin/env python3
"""Compare baseline and grouped emitted accesses with the real permission runtime."""
import hashlib,json,os,pathlib,re,subprocess
from checked_codegen import command as codegen_command
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/guest-access-groups';out.mkdir(parents=True,exist_ok=True)
tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin')
plugins=[R/'generated/tools'/n/(s+'.dylib') for n,s in [('guest-memory-pass','GuestMemoryPass'),('external-memory-pass','ExternalMemoryPass')]]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for p in plugins:
 d=json.loads(p.with_name('result.json').read_text());assert sha(p)==d['plugin_sha256'] and sha(R/'port/compiler'/(p.stem+'.cpp'))==d['source_sha256']
lines=['target triple = "arm64-apple-macos12"','target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"','declare void @revoke_page()','define void @batch(ptr %p,ptr %host) {','%h = load i64,ptr %host','%hn = add i64 %h,1','store i64 %hn,ptr %host']
for i in range(16):lines += ['%%v%d = load i64,ptr %%p'%i,'%%n%d = add i64 %%v%d,1'%(i,i),'store i64 %%n%d,ptr %%p'%i]
lines+=['ret void','}','define i64 @publication_boundary(ptr %p) {','%a = load i64,ptr %p','call void @revoke_page()','%b = load i64,ptr %p','store i64 %b,ptr %p','ret i64 %b','}', 'define void @boundaries(ptr %p) {','%a = load i64,ptr %p','fence seq_cst','%b = load volatile i64,ptr %p','%c = atomicrmw add ptr %p,i64 1 seq_cst','br label %next','next:','store i64 %a,ptr %p','ret void','}']
lines+=['define void @read_batch(ptr %p,ptr %dest) {']
for i in range(16):lines += ['%%v%d = load i64,ptr %%p'%i,'store i64 %%v%d,ptr %%dest'%i]
lines+=['ret void','}']
# Fifteen compiler-owned stores before the first guest access must not spend
# the group's budget. The same stores inside a bracket still consume it.
for name,interleaved in [('host_prelude',False),('host_interleaved',True)]:
 lines+=['define void @'+name+'(ptr %p) {','%local = alloca i64']
 if interleaved:lines+=['%initial = load i64,ptr %p']
 for i in range(15):lines+=['store i64 %d,ptr %%local'%i]
 for i in range(1 if interleaved else 4):
  lines+=['%%v%d = load i64,ptr %%p'%i,'%%n%d = add i64 %%v%d,1'%(i,i),'store i64 %%n%d,ptr %%p'%i]
 lines+=['ret void','}']
fixture=out/'input.ll';fixture.write_text('\n'.join(lines)+'\n')
policy=out/'policy.json';policy.write_text(json.dumps(dict(checked_symbols=['revoke_page'],host_only=[],range_checked=[],memory_manager_calls=[],memory_manager_sources=[],host_intrinsics=[],checked_strings=[],checked_callbacks=[],barrier_asm_sources=[],scalar_fcntl_commands=[])))
env=dict(os.environ,TRIBES_EXTERNAL_POLICY=str(policy));results={}
for name in ['baseline','grouped','separate-ends']:
 grouped=name!='baseline';ir=out/(name+'.ll')
 cmd=[str(tools/'opt')]+[a for p in plugins for a in ['-load-pass-plugin',str(p)]]+['-passes=tribes-guest-memory-cpu-host'+(',tribes-external-memory' if grouped else '')+',verify','-S',str(fixture),'-o',str(ir)]
 run=subprocess.run(cmd,env=env,capture_output=True,text=True);assert not run.returncode,run.stderr
 (out/(name+'-pass.log')).write_text(run.stderr)
 if name=='separate-ends':
  # Reconstruct the previous implementation with identical checks and lock
  # boundaries, changing only the batched end back into individual ends.
  pending=[];old=[]
  for line in ir.read_text().splitlines():
   if 'call i32 @tribes_memory_access_begin(' in line:pending.append(line.strip().split(' = ')[0])
   if 'call void @tribes_memory_access_end_many(' in line:
    old.extend('  call void @tribes_memory_access_end(i32 '+t+')' for t in pending);pending=[]
   else:
    old.append(line)
    if 'call void @tribes_memory_access_end(' in line:pending.remove(re.search(r'\(i32 ([^)]+)\)',line)[1])
  assert not pending;ir.write_text('\n'.join(old)+'\n')
 # Check each begin still immediately precedes its access, and no outstanding
 # group crosses control flow, calls, fences or an unrelated memory operation.
 pending=[];sums={};maximum=0;begins=0;ends=0;previous='';batched=0
 for line in ir.read_text().splitlines():
  s=line.strip()
  if 'call i32 @tribes_memory_access_begin(' in s:
   token=s.split(' = ')[0];pending.append(token);sums[token]=[token];begins+=1;maximum=max(maximum,len(pending))
  elif re.match(r'%tribes.end.count[^ ]* = add i32 ',s):
   key,args=s.split(' = add i32 ');left,right=args.split(', ');sums[key]=sums[left]+sums[right]
  elif 'call void @tribes_memory_access_end_many(' in s:
   tokens=sums[re.search(r'\(i32 ([^)]+)\)',s)[1]]
   assert tokens==pending and 2<=len(tokens)<=16,(tokens,pending)
   ends+=len(tokens);pending=[];batched+=1
  elif 'call void @tribes_memory_access_end(' in s:
   token=re.search(r'\(i32 ([^)]+)\)',s)[1];assert token in pending;pending.remove(token);ends+=1
  elif '!tribes.checked.host.local' in s:
   assert re.search(r'\b(load|store)\b',s) and 'volatile' not in s
  elif re.search(r'\b(load|store|atomicrmw)\b',s):
   assert '@tribes_memory_access_begin(' in previous,s
   if 'volatile' in s or 'atomicrmw' in s:assert len(pending)==1
  elif re.search(r'\b(call|br|ret|fence)\b',s):assert not pending,s
  previous=s
 assert not pending and begins==ends and maximum==(16 if grouped else 1),(pending,begins,ends,maximum)
 assert (batched>0)==(name=='grouped')
 if name=='grouped':
  text=ir.read_text()
  prelude=re.search(r'define void @host_prelude\([^}]+}',text)[0]
  interleaved=re.search(r'define void @host_interleaved\([^}]+}',text)[0]
  assert prelude.count('call i32 @tribes_memory_access_begin(')==8
  assert prelude.count('call void @tribes_memory_access_end_many(')==1
  assert 'call void @tribes_memory_access_end(' not in prelude
  # The first read must retire before later guest accesses after fifteen native stores.
  assert interleaved.count('call void @tribes_memory_access_end(')==1
  assert interleaved.count('call void @tribes_memory_access_end_many(')==1
 obj=out/(name+'.o');subprocess.run(codegen_command(tools,'arm64-apple-macos12',ir,obj),check=True)
 for tsan in [False,True]:
  exe=out/(name+('-tsan' if tsan else ''))
  sources=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c',R/'tests/guest_access_groups_test.c']
  subprocess.run(['clang','-O2','-pthread','-Wall','-Wextra','-Werror']+(['-fsanitize=thread'] if tsan else [])+list(map(str,sources))+[str(obj),'-o',str(exe)],check=True)
  run=subprocess.run([str(exe)]+(['concurrency-only'] if tsan else []),capture_output=True,text=True,timeout=45);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
  results[exe.name]=dict(output=run.stdout.strip(),binary_sha256=sha(exe),seconds=float(re.search(r'batches ([0-9.]+) seconds',run.stdout)[1]))
(out/'result.json').write_text(json.dumps(dict(passed=True,results=results,sources_sha256={str(p.relative_to(R)):sha(p) for p in [R/'port/compiler/ExternalMemoryPass.cpp',R/'tests/guest_access_groups_test.c',R/'port/core/guest_memory_checks.c',R/'port/core/guest_memory_checks.h',pathlib.Path(__file__).resolve()]},scope='Emitted permission checks and bounded grouping; microbenchmark is not game FPS.'),indent=2)+'\n');print(json.dumps(results,indent=2))
