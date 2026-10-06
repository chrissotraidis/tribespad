#!/usr/bin/env python3
"""Exercise generated public AOT routes and the actual phase guard."""
import hashlib,json,pathlib,re,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=R/'generated/srw/run-20261002T170057-ifc22/native-diagnostic/checked/bootstrap-published';meta=json.loads((source/'late-patch-inputs.json').read_text());names=meta['functions'];engine=source/'engine-routed.ll';assert sha(engine)==meta['outputs_sha256'][str(engine.relative_to(R))]
s=engine.read_text();out=R/'generated/integration-review/late-route-tests';out.mkdir(exist_ok=True);llvm=['target triple = "arm64-apple-macos12"','%_cpu = type { i32 }','declare i32 @qol_late_route_state()','declare void @observe(ptr, i32)'];c=['#include <assert.h>','#include <stdint.h>','#include <stdlib.h>','#include <string.h>','#include <unistd.h>','#include <sys/wait.h>','typedef struct { unsigned char bytes[440]; } _cpu;','static uint64_t base=1; static unsigned phase,seen; static _cpu saved;','uint32_t tribes_memory_code_revision(uint64_t b){assert(b==base);return phase;}']
harness=R/'tests/qol_native_bootstrap.c';helper=re.search(r'^int qol_late_route_state\(void\)\{[^\n]+',harness.read_text(),re.M);assert helper;c.append(helper[0]);c.append('void observe(_cpu *cpu,unsigned id){assert(!memcmp(cpu,&saved,sizeof saved));seen=id;}')
for i,name in enumerate(names):
 start=s.index('define hidden fastcc void @'+name+'(');end=s.index('\n}',start)+2
 llvm.append(s[start:end]);c.append('extern void '+name+'(_cpu *);')
 for variant,extra in [('initial',0),('late',100)]:llvm.append('define hidden fastcc void @tribes_%s_%s(ptr %%cpu) { call void @observe(ptr %%cpu, i32 %d)\n ret void\n}'%(variant,name,i+1+extra))
c.append('int main(void){_cpu cpu;for(unsigned i=0;i<sizeof cpu;i++)cpu.bytes[i]=(unsigned char)(i*73+9);saved=cpu;')
for phase in [1,2]:
 c.append('phase=%d;'%phase)
 for i,name in enumerate(names):c.append('seen=0;%s(&cpu);assert(seen==%d&&!memcmp(&cpu,&saved,sizeof cpu));'%(name,i+1+(100 if phase==2 else 0)))
c.append('for(unsigned p=0;p<4;p+=3){pid_t child=fork();assert(child>=0);if(!child){phase=p;'+names[0]+'(&cpu);_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);}return 0;}')
(out/'routes.ll').write_text('\n'.join(llvm)+'\n');(out/'test.c').write_text('\n'.join(c)+'\n');exe=out/'test'
subprocess.run(['/opt/homebrew/Cellar/llvm@20/20.1.8/bin/clang','-target','arm64-apple-macos12','-O2','-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip(),str(out/'routes.ll'),str(out/'test.c'),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/'stderr.log').write_text(run.stderr);assert run.returncode==0,run.stderr
report=dict(passed=True,routes=len(names),phase_checks=54,rejected_phases=[0,3],cpu_bytes_preserved=440,engine_sha256=sha(engine),harness_sha256=sha(harness),script_sha256=sha(pathlib.Path(__file__).resolve()),binary_sha256=sha(exe),scope='Generated route wrappers call the selected initial/later entry without changing CPU state; actual harness guard rejects unpublished/partial revision states. Function behavior is tested separately.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
