#!/usr/bin/env python3
"""Exercise actual nested-revision wrapper bodies and unchanged CPU state."""
import argparse,hashlib,json,pathlib,re,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();p=argparse.ArgumentParser();p.add_argument('source',type=pathlib.Path);a=p.parse_args();source=a.source.resolve();meta=json.loads((source/'patch-inputs.json').read_text());engine=source/'engine-routed.ll';assert sha(engine)==meta['sources_sha256'][str(engine.relative_to(R))];s=engine.read_text();out=R/'generated/integration-review/network-routes';out.mkdir(exist_ok=True)
network=set(meta['network_patch']['functions']);network_added=set(meta['network_patch']['added']);network_name=lambda name:'tribes_before_network_'+name if name in network else name
followup=set(meta['followup_patch']['functions']);added=set(meta['followup_patch']['added']);prior_name=lambda name:'tribes_before_followup_'+name if name in followup else network_name(name)
routes=[(prior_name(n),'tribes_initial_'+n,'tribes_late_'+n,2) for n in meta['late_patch']['functions']]+[(prior_name('loc_556CFF'),'tribes_before_input_556CFF','tribes_after_input_556CFF',4)]
library=json.loads((R/'generated/integration-review/library-routes/result.json').read_text());routes += [(r['router'],'loc_%X'%r['old_target'],'loc_%X'%r['new_target'],6) for r in library['routes']]
routes += [(network_name(n),None if n in added else 'tribes_before_followup_'+n,'tribes_followup_'+n,8) for n in sorted(followup)]
routes += [(n,None if n in network_added else 'tribes_before_network_'+n,'tribes_network_'+n,10) for n in sorted(network)]
ids={name:i+1 for i,name in enumerate(sorted({v for r in routes for v in r[1:3] if v}))}
llvm=['target triple = "arm64-apple-macos12"','%_cpu = type { i32 }','declare void @observe(ptr, i32)','declare void @qol_followup_unavailable_entry() noreturn','declare void @qol_network_unavailable_entry() noreturn']
helpers=['qol_late_route_state','qol_input_route_ready','qol_library_route_ready','qol_followup_route_ready','qol_network_route_ready'];llvm+=['declare i32 @%s()'%h for h in helpers]
c=['#include <assert.h>','#include <stdint.h>','#include <stdlib.h>','#include <string.h>','#include <unistd.h>','#include <sys/wait.h>','typedef struct { unsigned char bytes[440]; } _cpu;','static uint64_t base=1; static unsigned phase,seen; static _cpu saved;','uint32_t tribes_memory_code_revision(uint64_t b){assert(b==base);return phase;}','_Noreturn void qol_followup_unavailable_entry(void){abort();}','_Noreturn void qol_network_unavailable_entry(void){abort();}']
harness=R/'tests/qol_native_bootstrap.c'
for h in helpers:c.append(re.search(r'^int '+h+r'\(void\)\{[^\n]+',harness.read_text(),re.M)[0])
c.append('void observe(_cpu *cpu,unsigned id){assert(!memcmp(cpu,&saved,sizeof saved));seen=id;}')
for i,(name,before,after,threshold) in enumerate(routes):
 needle='define hidden fastcc void @'+name+'(';start=s.index(needle);end=s.index('\n}',start)+2;body=s[start:end];body=body.replace(needle,'define hidden fastcc void @route_test_%d('%i,1);llvm.append(body);c.append('extern void route_test_%d(_cpu *);'%i)
for name,identifier in ids.items():llvm.append('define hidden fastcc void @%s(ptr %%cpu) { call void @observe(ptr %%cpu, i32 %d)\n ret void\n}'%(name,identifier))
c.append('int main(void){_cpu cpu;for(unsigned i=0;i<sizeof cpu;i++)cpu.bytes[i]=(unsigned char)(i*73+9);saved=cpu;');checks=0;faults=0
for phase in [1,2,4,6,8,10]:
 c.append('phase=%d;'%phase)
 for i,(name,before,after,threshold) in enumerate(routes):
  expected=after if phase>=threshold else before
  if expected:c.append('seen=0;route_test_%d(&cpu);assert(seen==%d&&!memcmp(&cpu,&saved,sizeof cpu));'%(i,ids[expected]));checks+=1
  else:c.append('{pid_t child=fork();assert(child>=0);if(!child){route_test_%d(&cpu);_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);}'%i);faults+=1
for h in helpers:
 for phase in [0,3,5,7,9,11]:
  c.append('{pid_t child=fork();assert(child>=0);if(!child){phase=%d;%s();_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);}'%(phase,h));faults+=1
c.append('return 0;}');(out/'routes.ll').write_text('\n'.join(llvm)+'\n');(out/'test.c').write_text('\n'.join(c)+'\n');exe=out/'test';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module',str(out/'routes.ll'),str(out/'test.c'),'-o',str(exe)],check=True);run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/'stderr.log').write_text(run.stderr);assert run.returncode==0,run.stderr
report=dict(passed=True,routes=len(routes),checks=checks,expected_faults=faults,states=[1,2,4,6,8,10],cpu_bytes_preserved=440,engine_sha256=sha(engine),harness_sha256=sha(harness),script_sha256=sha(pathlib.Path(__file__).resolve()),binary_sha256=sha(exe),scope='Actual prior and outer wrapper bodies tested individually, with all finite phase helpers and440 CPU bytes preserved. Added continuation entries stop before their respective states8/10. Callees observed; full native prefix execution is a separate check.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',checks,'route checks and',faults,'expected stops')
