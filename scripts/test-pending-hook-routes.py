#!/usr/bin/env python3
"""Exercise actual nested-revision wrapper bodies and unchanged CPU state."""
import argparse,hashlib,json,pathlib,re,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();p=argparse.ArgumentParser();p.add_argument('source',type=pathlib.Path);a=p.parse_args();source=a.source.resolve();meta=json.loads((source/'patch-inputs.json').read_text());engine=source/'engine-routed.ll';assert sha(engine)==meta['sources_sha256'][str(engine.relative_to(R))];s=engine.read_text()+(source/'startup-calls-engine.ll').read_text()+(source/'pending-hooks-bank.ll').read_text()+(source/'pending-hooks-continuations.ll').read_text();out=R/'generated/integration-review/pending-hook-routes';out.mkdir(exist_ok=True)
network=set(meta['network_patch']['functions']);network_added=set(meta['network_patch']['added']);network_name=lambda name:'tribes_before_network_'+name if name in network else name
followup=set(meta['followup_patch']['functions']);added=set(meta['followup_patch']['added']);prior_name=lambda name:'tribes_before_followup_'+name if name in followup else network_name(name)
routes=[(prior_name(n),'tribes_initial_'+n,'tribes_late_'+n,2) for n in meta['late_patch']['functions']]+[(prior_name('loc_556CFF'),'tribes_before_input_556CFF','tribes_after_input_556CFF',4)]
library=json.loads((R/'generated/integration-review/library-routes/result.json').read_text());routes += [(r['router'],'loc_%X'%r['old_target'],'loc_%X'%r['new_target'],6) for r in library['routes']]
routes += [(network_name(n),None if n in added else 'tribes_before_followup_'+n,'tribes_followup_'+n,8) for n in sorted(followup)]
routes += [(n,None if n in network_added else 'tribes_before_network_'+n,'tribes_network_'+n,10) for n in sorted(network)]
http=set(meta['http_patch']['functions']);http_added=set(meta['http_patch']['added'])
routes=[('tribes_before_http_'+name if name in http else name,before,after,threshold) for name,before,after,threshold in routes]
routes += [(n,None if n in http_added else 'tribes_before_http_'+n,'tribes_http_'+n,12) for n in sorted(http)]
string=set(meta['string_patch']['functions']);string_added=set(meta['string_patch']['added'])
routes=[('tribes_before_string_'+name if name in string else name,before,after,threshold) for name,before,after,threshold in routes]
routes += [(n,None if n in string_added else 'tribes_before_string_'+n,'tribes_string_'+n,14) for n in sorted(string)]

routes += [('loc_5D2842','tribes_before_console_fade_loc_5D2842','tribes_console_fade_loc_5D2842',16)]
routes += [(n,'tribes_before_animation_'+n,'tribes_animation_'+n,18) for n in ['loc_67CC8F','loc_67D8FC']]
routes += [('loc_5BFF77','tribes_before_post_file_loc_5BFF77','tribes_post_file_loc_5BFF77',20)]
routes += [('loc_617BA0','tribes_before_collision_loc_617BA0','tribes_collision_loc_617BA0',22)]
routes += [('loc_615F00','tribes_before_allocation_loc_615F00','tribes_allocation_loc_615F00',24)]
routes += [(n,'tribes_before_clock_'+n,'tribes_clock_'+n,26) for n in ['loc_58A960','loc_50CB12']]
routes += [(n,'tribes_before_post_video_'+n,'tribes_post_video_'+n,28) for n in ['loc_5BB95E','loc_561683']]
routes += [(n,'tribes_before_post_video_nop_'+n,'tribes_post_video_nop_'+n,30) for n in ['loc_4025E2','loc_4025FD']]
routes += [(n,'tribes_before_post_video_nop_second_'+n,'tribes_post_video_nop_second_'+n,32) for n in ['loc_402540','loc_40255B']]
routes += [(n,'tribes_before_post_video_tail_'+n,'tribes_post_video_tail_'+n,34) for n in ['loc_4023F0','loc_4B3C70','loc_4C65D0']]
routes += [(n,'tribes_before_display_'+n,'tribes_display_'+n,36) for n in ['loc_561250','loc_561272']]
routes += [(n,'tribes_before_startup_calls_'+n,'tribes_startup_calls_'+n,38) for n in ['loc_410C79','loc_40A130','loc_40A640']]
routes += [('loc_410C7E',None,'loc_410C7F',38)]
routes += [(n,'tribes_before_input_nop_'+n,'tribes_input_nop_'+n,40) for n in ['loc_556806','loc_55680B']]
pending={r['name'] for r in meta['pending_hooks']['functions']}
routes=[('tribes_before_pending_'+name if name in pending else name,before,after,threshold) for name,before,after,threshold in routes]
routes += [(r['name'],'tribes_before_pending_'+r['name'],r['target'],42) for r in meta['pending_hooks']['functions']]
routes += [(n,None,'tribes_pending_'+n,42) for n in meta['pending_hooks']['added']+[r['symbol'] for r in meta['pending_hooks']['bank_entries']]]
assert len({r[0] for r in routes})==len(routes)
ids={name:i+1 for i,name in enumerate(sorted({v for r in routes for v in r[1:3] if v}))}
llvm=['target triple = "arm64-apple-macos12"','%_cpu = type { i32 }','declare void @observe(ptr, i32)','declare void @qol_followup_unavailable_entry() noreturn','declare void @qol_network_unavailable_entry() noreturn','declare void @qol_http_unavailable_entry() noreturn','declare void @qol_string_unavailable_entry() noreturn','declare void @qol_startup_calls_unavailable() noreturn','declare void @qol_pending_hooks_unavailable() noreturn']
helpers=['qol_late_route_state','qol_input_route_ready','qol_library_route_ready','qol_followup_route_ready','qol_network_route_ready','qol_http_route_ready','qol_string_route_ready','qol_console_fade_route_ready','qol_console_animation_route_ready','qol_post_file_patch_route_ready','qol_collision_patch_route_ready','qol_allocation_patch_route_ready','qol_clock_patch_route_ready','qol_post_video_prefix_route_ready','qol_post_video_nop_route_ready','qol_post_video_nop_second_route_ready','qol_post_video_tail_route_ready','qol_display_immediates_route_ready','qol_startup_calls_route_ready','qol_input_nop_route_ready','qol_pending_hooks_route_ready'];llvm+=['declare i32 @%s()'%h for h in helpers]
c=['#include <assert.h>','#include <stdint.h>','#include <stdlib.h>','#include <string.h>','#include <unistd.h>','#include <sys/wait.h>','typedef struct { unsigned char bytes[440]; } _cpu;','static uint64_t base=1; static unsigned phase,seen,completed; static _cpu saved;','uint32_t tribes_memory_code_revision(uint64_t b){assert(b==base);return phase;}','uint32_t tribes_memory_code_completed_revision(uint64_t b){assert(b==base);return completed;}','_Noreturn void qol_followup_unavailable_entry(void){abort();}','_Noreturn void qol_network_unavailable_entry(void){abort();}','_Noreturn void qol_http_unavailable_entry(void){abort();}','_Noreturn void qol_string_unavailable_entry(void){abort();}','_Noreturn void qol_startup_calls_unavailable(void){abort();}','_Noreturn void qol_pending_hooks_unavailable(void){abort();}']
harness=R/'tests/qol_native_bootstrap.c'
c.append(re.search(r'^static uint32_t qol_prior_route_phase\(void\)\{.*?^}',harness.read_text(),re.M|re.S)[0])
for h in helpers:c.append(re.search(r'^int '+h+r'\(void\)\{[^\n]+',harness.read_text(),re.M)[0])
c.append('void observe(_cpu *cpu,unsigned id){assert(!memcmp(cpu,&saved,sizeof saved));seen=id;}')
offsets={m[1]:m.start() for m in re.finditer(r'^define hidden fastcc void @(\w+)\(',s,re.M)}
for i,(name,before,after,threshold) in enumerate(routes):
 needle='define hidden fastcc void @'+name+'(';start=offsets[name];end=s.index('\n}',start)+2;body=s[start:end];body=body.replace(needle,'define hidden fastcc void @route_test_%d('%i,1);llvm.append(body);c.append('extern void route_test_%d(_cpu *);'%i)
for name,identifier in ids.items():llvm.append('define hidden fastcc void @%s(ptr %%cpu) { call void @observe(ptr %%cpu, i32 %d)\n ret void\n}'%(name,identifier))
c.append('int main(void){_cpu cpu;for(unsigned i=0;i<sizeof cpu;i++)cpu.bytes[i]=(unsigned char)(i*73+9);saved=cpu;');checks=0;faults=0
for phase in [1,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54]:
 c.append('phase=%d;'%phase)
 for i,(name,before,after,threshold) in enumerate(routes):
  expected=after if phase>=threshold else before
  if expected:c.append('seen=0;route_test_%d(&cpu);assert(seen==%d&&!memcmp(&cpu,&saved,sizeof cpu));'%(i,ids[expected]));checks+=1
  else:c.append('{pid_t child=fork();assert(child>=0);if(!child){route_test_%d(&cpu);_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);}'%i);faults+=1
# Only entries already proven disjoint and completely published are available.
for completed in [26,28,30,32,34,36,38,40,42,44,46,48,50,52]:
 c.append('phase=3;completed=%d;'%completed)
 for i,(name,before,after,threshold) in enumerate(routes):
  if threshold<28 or (threshold==28 and completed>=28) or (threshold==30 and completed>=30) or (threshold==32 and completed>=32) or (threshold==34 and completed>=34) or (threshold==36 and completed>=36) or (threshold==38 and completed>=38) or (threshold==40 and completed>=40) or (threshold==42 and completed>=42):c.append('seen=0;route_test_%d(&cpu);assert(seen==%d&&!memcmp(&cpu,&saved,sizeof cpu));'%(i,ids[after]));checks+=1
  else:c.append('{pid_t child=fork();assert(child>=0);if(!child){route_test_%d(&cpu);_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);}'%i);faults+=1
c.append('completed=0;')
for h in helpers:
 for phase in [0,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43,45,47,49,51,53,55,56]:
  c.append('{pid_t child=fork();assert(child>=0);if(!child){phase=%d;%s();_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);}'%(phase,h));faults+=1
c.append('return 0;}');(out/'routes.ll').write_text('\n'.join(llvm)+'\n');(out/'test.c').write_text('\n'.join(c)+'\n');exe=out/'test';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module',str(out/'routes.ll'),str(out/'test.c'),'-o',str(exe)],check=True);run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120);(out/'stderr.log').write_text(run.stderr);assert run.returncode==0,run.stderr
report=dict(passed=True,routes=len(routes),checks=checks,expected_faults=faults,states=[1,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54],cpu_bytes_preserved=440,engine_sha256=sha(engine),harness_sha256=sha(harness),script_sha256=sha(pathlib.Path(__file__).resolve()),binary_sha256=sha(exe),scope='Actual production route wrappers through revision27, preserving all440 CPU bytes. The34 detours,36 bank and17 continuation entries activate at42 and stop during partial publication; completed disjoint earlier routes remain usable. Callees are observation endpoints, not game acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',checks,'route checks and',faults,'expected stops')
