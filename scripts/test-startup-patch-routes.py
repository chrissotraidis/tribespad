#!/usr/bin/env python3
"""Check all integrated generated routes through three finite revisions."""
import argparse,hashlib,json,pathlib,re,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();p=argparse.ArgumentParser();p.add_argument('source',type=pathlib.Path);a=p.parse_args();source=a.source.resolve();meta=json.loads((source/'patch-inputs.json').read_text());engine=source/'engine-routed.ll';assert sha(engine)==meta['sources_sha256'][str(engine.relative_to(R))]
s=engine.read_text();out=R/'generated/integration-review/library-routes/phases';out.mkdir(exist_ok=True)
names=meta['late_patch']['functions'];routes=[(n,'tribes_initial_'+n,'tribes_late_'+n,2) for n in names]+[('loc_556CFF','tribes_before_input_556CFF','tribes_after_input_556CFF',4)]
library=json.loads((R/'generated/integration-review/library-routes/result.json').read_text());routes += [(r['router'],'loc_%X'%r['old_target'],'loc_%X'%r['new_target'],6) for r in library['routes']]
identifiers={name:i+1 for i,name in enumerate(sorted({v for r in routes for v in r[1:3]}))}
llvm=['target triple = "arm64-apple-macos12"','%_cpu = type { i32 }','declare i32 @qol_late_route_state()','declare i32 @qol_input_route_ready()','declare i32 @qol_library_route_ready()','declare void @observe(ptr, i32)']
c=['#include <assert.h>','#include <stdint.h>','#include <stdlib.h>','#include <string.h>','#include <unistd.h>','#include <sys/wait.h>','typedef struct { unsigned char bytes[440]; } _cpu;','static uint64_t base=1; static unsigned phase,seen; static _cpu saved;','uint32_t tribes_memory_code_revision(uint64_t b){assert(b==base);return phase;}']
harness=R/'tests/qol_native_bootstrap.c'
for helper in ['qol_late_route_state','qol_input_route_ready','qol_library_route_ready']:
 m=re.search(r'^int '+helper+r'\(void\)\{[^\n]+',harness.read_text(),re.M);assert m;c.append(m[0])
c.append('void observe(_cpu *cpu,unsigned id){assert(!memcmp(cpu,&saved,sizeof saved));seen=id;}')
for name,before,after,threshold in routes:
 start=s.index('define hidden fastcc void @'+name+'(');end=s.index('\n}',start)+2;llvm.append(s[start:end]);c.append('extern void '+name+'(_cpu *);')
for variant,identifier in identifiers.items():llvm.append('define hidden fastcc void @%s(ptr %%cpu) { call void @observe(ptr %%cpu, i32 %d)\n ret void\n}'%(variant,identifier))
c.append('int main(void){_cpu cpu;for(unsigned i=0;i<sizeof cpu;i++)cpu.bytes[i]=(unsigned char)(i*73+9);saved=cpu;')
for phase in [1,2,4,6]:
 c.append('phase=%d;'%phase)
 for name,before,after,threshold in routes:
  c.append('seen=0;%s(&cpu);assert(seen==%d&&!memcmp(&cpu,&saved,sizeof cpu));'%(name,identifiers[after if phase>=threshold else before]))
for name in [names[0],'loc_556CFF',library['routes'][0]['router']]:
 c.append('for(unsigned p=0;p<8;p++){if(p==1||p==2||p==4||p==6)continue;pid_t child=fork();assert(child>=0);if(!child){phase=p;'+name+'(&cpu);_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);}')
c.append('return 0;}');(out/'routes.ll').write_text('\n'.join(llvm)+'\n');(out/'test.c').write_text('\n'.join(c)+'\n');exe=out/'test'
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module',str(out/'routes.ll'),str(out/'test.c'),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/'stderr.log').write_text(run.stderr);assert run.returncode==0,run.stderr
report=dict(passed=True,routes=len(routes),phase_checks=len(routes)*4,rejected_phases=[0,3,5,7],cpu_bytes_preserved=440,engine_sha256=sha(engine),harness_sha256=sha(harness),script_sha256=sha(pathlib.Path(__file__).resolve()),binary_sha256=sha(exe),scope='Integrated route wrappers and actual harness phase helpers retain earlier routes through state6 and activate the25 new redirects only at state6. Observed callees preserve complete CPU. No downstream execution claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(routes)*4,'route phase checks and twelve forbidden-phase stops')
