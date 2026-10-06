#!/usr/bin/env python3
"""Exercise emitted renderer hook routes, gates and prior revision selectors."""
import hashlib,json,pathlib,re,subprocess
from renderer_hook_runtime_inputs import prepare,definition
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/renderer-hook-bank';out=P/'routing';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
plan=json.loads((P/'plan.json').read_text());old=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';source=(old/'engine-routed.ll').read_text();cpu=re.search(r'^%_cpu = type .*$',source,re.M)[0];engine=out/'engine.ll';engine.write_text(cpu+'\n'+''.join(definition(source,'loc_%X'%r['original_target']) for r in plan['trampolines']));_,sources,m=prepare('',engine,out)
llvm=[cpu,'declare i32 @qol_renderer_hooks_route_ready()','declare i32 @qol_renderer_nested_exact_ready()','declare void @qol_renderer_hooks_unavailable()','declare void @observe(ptr,i32)'];c=['#include <assert.h>','#include <stdint.h>','#include <string.h>','#include <stdlib.h>','#include <unistd.h>','#include <sys/wait.h>','static uint64_t base=1;static uint32_t phase,completed,seen;static unsigned char saved[440];','uint32_t tribes_memory_code_revision(uint64_t b){assert(b==base);return phase;}','uint32_t tribes_memory_code_completed_revision(uint64_t b){assert(b==base);return completed;}','void observe(void *p,unsigned v){assert(!memcmp(p,saved,440));seen=v;}','void qol_renderer_hooks_unavailable(void){abort();}']
h=(R/'tests/qol_native_bootstrap.c').read_text();c.append(re.search(r'^static uint32_t qol_prior_route_phase\(void\)\{.*?^}',h,re.M|re.S)[0]);prior=[]
for line in h.splitlines():
 if line.startswith(('int qol_audio_tables_route_ready(','int qol_audio_hooks_route_ready(')):continue
 if re.match(r'int qol_\w+route_ready\(void\)',line) or line.startswith('int qol_renderer_nested_route_ready('):
  c.append(line)
  name=re.match(r'int (\w+)',line)[1]
  if name not in ['qol_renderer_hooks_route_ready','qol_audio_tables_route_ready','qol_audio_hooks_route_ready'] and '(void)' in line:prior.append(name)
c.append(re.search(r'^int qol_renderer_nested_exact_ready\(void\).*$',h,re.M)[0])
rows=[];stubs={}
for r in m['functions']:
 name=r['name'];rows.append((name,'router'));llvm.append(definition(engine.read_text(),name));stubs['tribes_before_renderer_hooks_'+name]=1;stubs[r['target']]=2
for label in ['bank','continuations']:
 text=(out/('renderer-hooks-'+label+'.ll')).read_text()
 for name in re.findall(r'^define hidden fastcc void @((?:loc_|tbpatch_)\w+)\(',text,re.M):
  rows.append((name,'exact' if name=='loc_6B0C21' else 'gate'));llvm.append(definition(text,name));stubs['tribes_renderer_hooks_'+name]=2
for name,v in stubs.items():llvm.append('define hidden fastcc void @%s(ptr %%cpu){call void @observe(ptr %%cpu,i32 %d)\nret void\n}'%(name,v))
for i,(name,kind) in enumerate(rows):llvm.append('define void @enter%d(ptr %%cpu){call fastcc void @%s(ptr %%cpu)\nret void\n}'%(i,name));c.append('extern void enter%d(void *);'%i)
c += ['static unsigned valid,denied;','static void check(void (*fn)(void *),unsigned want){unsigned char cpu[440];memcpy(cpu,saved,440);if(want){seen=0;fn(cpu);assert(seen==want);valid++;}else{pid_t p=fork();assert(p>=0);if(!p){fn(cpu);_exit(0);}int s;assert(waitpid(p,&s,0)==p&&WIFSIGNALED(s)&&WTERMSIG(s)==6);denied++;}}','int main(void){memset(saved,0xa5,440);']
for i,(_,kind) in enumerate(rows):
 if kind=='router':c.append('for(phase=1;phase<=62;phase=phase==1?2:phase+2)check(enter%d,phase==62?2:1);'%i)
 else:c.append('phase=60;check(enter%d,%d);phase=62;check(enter%d,2);'%(i,2 if kind=='exact' else 0,i))
 c.append('phase=3;completed=60;check(enter%d,%d);phase=64;check(enter%d,2);phase=66;check(enter%d,2);phase=68;check(enter%d,0);'%(i,2 if kind=='exact' else 0,i,i,i))
c += ['phase=3;completed=60;'+''.join('assert(%s());'%n for n in prior),'return 0;}'];(out/'test.c').write_text('\n'.join(c));(out/'test.ll').write_text('\n'.join(llvm));exe=out/'test';subprocess.run(['clang','-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip(),'-O2','-Wno-override-module',str(out/'test.c'),str(out/'test.ll'),'-o',str(exe)],check=True);run=subprocess.run([str(exe)],capture_output=True,text=True);(out/'stderr.log').write_text(run.stderr);assert not run.returncode,run.stderr
report=dict(passed=True,routes=len(rows),prior_selectors=len(prior),binary_sha256=sha(exe),harness_sha256=sha(R/'tests/qol_native_bootstrap.c'),script_sha256=sha(pathlib.Path(__file__).resolve()),runtime_sha256=sha(R/'scripts/renderer_hook_runtime_inputs.py'),scope='Actual26 emitted entry routers,53 bank/continuation gates, partial/future rejection and all earlier single-group selectors at partial31. Continuation21 remains callable after its unchanged nested revision.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(rows),'emitted renderer routes/gates;',len(prior),'prior selectors')
