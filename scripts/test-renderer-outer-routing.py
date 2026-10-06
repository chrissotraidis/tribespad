#!/usr/bin/env python3
"""Exercise nine emitted routers and the prior renderer during publication."""
import hashlib,json,pathlib,re,subprocess
from renderer_outer_runtime_inputs import definition
from renderer_outer_runtime_inputs import prepare
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/renderer-outer';out=P/'routing';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((P/'preparation.json').read_text());source=(R/'generated/integration-review/string-patched-engine/va/engine.ll').read_text();engine=out/'engine.ll';engine.write_text(''.join(definition(source,r['name']) for r in m['records']));dll=(R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll').read_text()
# Exercise the same existing shared-store mediation that integrated preparation receives.
mediated=(R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe/qol-patched.ll').read_text()
for name in {w['writer_name'] for w in m['writes'] if w.get('shared')}:
 dll=dll.replace(definition(dll,name),definition(mediated,name),1)
prepare(dll,engine,out)
cpu=re.search(r'^%_cpu = type .*$',source,re.M)[0];llvm=[cpu,'declare i32 @qol_renderer_outer_route_ready()','declare void @observe(ptr,i32)'];c=['#include <assert.h>','#include <stdint.h>','#include <string.h>','#include <unistd.h>','#include <sys/wait.h>','static uint64_t base=1;static uint32_t phase,completed,seen;static unsigned char saved[440];','uint32_t tribes_memory_code_revision(uint64_t b){assert(b==base);return phase;}','uint32_t tribes_memory_code_completed_revision(uint64_t b){assert(b==base);return completed;}','void observe(void *p,unsigned v){assert(!memcmp(p,saved,440));seen=v;}']
h=(R/'tests/qol_native_bootstrap.c').read_text();c += [re.search(r'^static uint32_t qol_prior_route_phase\(void\)\{.*?^}',h,re.M|re.S)[0]]
for n in ['qol_renderer_outer_route_ready','qol_renderer_call_route_ready']:c.append(re.search(r'^int '+n+r'\(void\)\{[^\n]+',h,re.M)[0])
for i,r in enumerate(m['records']):
 name=r['name'];llvm.append(definition(engine.read_text(),name))
 for prefix,v in [('tribes_before_renderer_outer_',1),('tribes_renderer_outer_',2)]:llvm.append(f'define hidden fastcc void @{prefix}{name}(ptr %cpu) {{call void @observe(ptr %cpu,i32 {v})\n ret void\n}}')
 llvm.append(f'define void @enter{i}(ptr %cpu) {{call fastcc void @{name}(ptr %cpu)\n ret void\n}}');c.append(f'extern void enter{i}(void *);')
c.append('''int main(void){unsigned char cpu[440];memset(cpu,0xa5,440);memcpy(saved,cpu,440);void (*routes[])(void *)={enter0,enter1,enter2,enter3,enter4,enter5,enter6,enter7,enter8};
 for(unsigned i=0;i<9;i++)for(phase=1;phase<=62;phase=phase==1?2:phase+2){seen=0;routes[i](cpu);assert(seen==(phase>=60?2:1));}
 unsigned bad[]={0,3,45,59,64};for(unsigned i=0;i<9;i++)for(unsigned j=0;j<5;j++){pid_t child=fork();assert(child>=0);if(!child){phase=bad[j];completed=58;routes[i](cpu);_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);}
 phase=3;completed=58;assert(qol_renderer_call_route_ready());phase=60;assert(qol_renderer_call_route_ready());return 0;}
''');(out/'test.c').write_text('\n'.join(c));(out/'test.ll').write_text('\n'.join(llvm));exe=out/'test';subprocess.run(['clang','-O2','-Wno-override-module',str(out/'test.c'),str(out/'test.ll'),'-o',str(exe)],check=True);run=subprocess.run([str(exe)],capture_output=True,text=True);(out/'stderr.log').write_text(run.stderr);assert run.returncode==0,run.stderr
(out/'result.json').write_text(json.dumps(dict(passed=True,routes=9,checks=288,expected_stops=45,prior_renderer_partial_preserved=True,binary_sha256=sha(exe),harness_sha256=sha(R/'tests/qol_native_bootstrap.c'),inputs_sha256=sha(R/'scripts/renderer_outer_runtime_inputs.py')),indent=2)+'\n');print('PASS: 9 actual routers,288 states,45 rejected states; preceding renderer remains callable during revision30')
