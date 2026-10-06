#!/usr/bin/env python3
"""Exercise the actual renderer router and mediated original writer."""
import hashlib,json,pathlib,re,subprocess
from console_fade_inputs import definition
from renderer_call_runtime_inputs import prepare
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-call/routing';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
meta=json.loads((out.parent/'preparation.json').read_text());candidate=(out.parent/'engine.ll').read_text();body=definition(candidate,'loc_505D13').replace('@loc_10046600(','@loc_4D0B40(');engine=out/'engine-routed.ll';engine.write_text(body)
original=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll';writer=definition(original.read_text(),'loc_10047071');dll,_,_=prepare(writer,engine,out)
router=definition(engine.read_text(),'loc_505D13');changed=definition(dll,'loc_10047071');cpu=re.search(r'^%_cpu = type .*$',candidate,re.M)[0]
llvm=cpu+'''\ndeclare i32 @qol_renderer_call_route_ready()
declare void @observe(ptr,i32)
declare void @qol_renderer_call_write(ptr,i32,ptr,i32,i64)
define hidden fastcc void @tribes_before_renderer_loc_505D13(ptr %cpu) {call void @observe(ptr %cpu,i32 1)\n ret void\n}
define hidden fastcc void @tribes_renderer_loc_505D13(ptr %cpu) {call void @observe(ptr %cpu,i32 2)\n ret void\n}
define hidden fastcc void @loc_10047073(ptr %cpu) {call void @observe(ptr %cpu,i32 3)\n ret void\n}
'''+router+changed+'''\ndefine void @enter_route(ptr %cpu) {call fastcc void @loc_505D13(ptr %cpu)\n ret void\n}
define void @enter_write(ptr %cpu) {call fastcc void @loc_10047071(ptr %cpu)\n ret void\n}
''';(out/'test.ll').write_text(llvm)
harness=R/'tests/qol_native_bootstrap.c';helper=re.search(r'^int qol_renderer_call_route_ready\(void\)\{[^\n]+',harness.read_text(),re.M)[0]
c='''#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
static uint64_t base=0x100000000ULL;static unsigned phase,seen,writes,completed;
static unsigned char saved[440];
uint32_t tribes_memory_code_revision(uint64_t b){assert(b==base);return phase;}
uint32_t tribes_memory_code_completed_revision(uint64_t b){assert(b==base);return completed;}
void observe(void *cpu,unsigned id){assert(!memcmp(cpu,saved,440));seen=id;}
void qol_renderer_call_write(void *cpu,uint32_t pc,void *p,uint32_t size,uint64_t value){assert(!memcmp(cpu,saved,440)&&pc==0x10047071&&(uintptr_t)p==base+0x505d4c&&size==4&&value==0xfb408b0);writes++;}
extern void enter_route(void *),enter_write(void *);
'''+re.search(r'^static uint32_t qol_prior_route_phase\(void\)\{.*?^}',harness.read_text(),re.M|re.S)[0]+'\n'+helper+'''
int main(void){unsigned char cpu[440];for(unsigned i=0;i<440;i++)cpu[i]=(unsigned char)(i*73+9);memcpy(cpu+112,&base,8);uint32_t ptr=0x505d4c,value=0xfb408b0;memcpy(cpu+12,&ptr,4);memcpy(cpu+28,&value,4);memcpy(saved,cpu,440);
for(phase=1;phase<=62;phase=phase==1?2:phase+2){seen=0;enter_route(cpu);assert(seen==(phase>=44?2:1));}
unsigned bad[]={0,3,41,43,45,47,49,51,55,64};for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){pid_t child=fork();assert(child>=0);if(!child){phase=bad[i];enter_route(cpu);_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);}
seen=0;enter_write(cpu);assert(seen==3&&writes==1&&!memcmp(cpu,saved,440));return 0;}
''';(out/'test.c').write_text(c);exe=out/'test';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module',str(out/'test.ll'),str(out/'test.c'),'-o',str(exe)],check=True);run=subprocess.run([str(exe)],capture_output=True,text=True);(out/'stderr.log').write_text(run.stderr);assert run.returncode==0,run.stderr
(out/'result.json').write_text(json.dumps(dict(passed=True,route_states=31,expected_stops=10,cpu_bytes=440,writer_preserved=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in [harness,R/'scripts/renderer_call_runtime_inputs.py',pathlib.Path(__file__).resolve()]},scope='Production renderer route/helper and original mediated writer; prior/new routing, partial/unknown rejection and unchanged440-byte CPU. Native code-store and concurrent state tests are separate.'),indent=2)+'\n');print('PASS: renderer route states, ten stops, actual writer operands and CPU preservation')
