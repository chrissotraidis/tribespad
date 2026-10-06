#!/usr/bin/env python3
"""Regression for the actual new flush branch under both memory guards."""
import hashlib,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-startup/flush';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
h=R/'tests/qol_native_bootstrap.c';s=h.read_text();a=s.index('uint32_t qol_publish_patches(');b=s.index(' if(stage==2&&pending_hook_write_count)',a);branch=s[a:b]+'return 0;\n}\n';branch=branch.replace('tribes_memory_code_revision(base)','tested_phase(base)')
source=out/'flush.c';source.write_text('#include <stdint.h>\n#include <assert.h>\n#include <stdio.h>\nuint64_t base;unsigned stage=2,engine_entered=1,renderer_startup_write_count=2,renderer_startup_flush_count;\nstatic unsigned tested_phase(uint64_t b){assert(b==base);return 3;}\n'+branch)
objects,checked=compile_support([source],out/'checked',[]);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded')
test=out/'test.c';test.write_text('''#include <stdint.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include "guest_memory_checks.h"
#include "guest_external_checks.h"
extern uint64_t base;extern unsigned renderer_startup_flush_count;
extern uint32_t qol_publish_patches(uint32_t,uint32_t,uint32_t);
int main(void){unsigned char *m=mmap(0,0x600000,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);assert(m!=MAP_FAILED);base=(uintptr_t)m;assert(tribes_memory_checks_configure(base,0x600000));tribes_memory_external_configure(base,0x600000);tribes_memory_external_enable();unsigned char bytes[]={0xe9,0x5b,1,0,0};memcpy(m+0x5a1130,bytes,5);
 assert(qol_publish_patches(UINT32_MAX,0x5a1130,5)==1&&renderer_startup_flush_count==1);
 for(unsigned i=0;i<3;i++){pid_t child=fork();assert(child>=0);if(!child){renderer_startup_flush_count=i==0; if(i==1)m[0x5a1134]=1;if(i==2)assert(tribes_memory_permissions_set(0x5a1000,4096,0));qol_publish_patches(UINT32_MAX,0x5a1130,5);_exit(99);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);m[0x5a1134]=0;}return 0;}
''');exe=out/'test';subprocess.run(['clang','-O1','-I',str(R/'port/core'),str(test),str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c'),*objects,'-o',str(exe)],check=True);run=subprocess.run([str(exe)],capture_output=True,text=True);(out/'stderr.log').write_text(run.stderr);assert run.returncode==0,run.stderr
assert 'external memory contract stop' not in run.stderr and run.stderr.count('checked guest memory violation')==1
(out/'result.json').write_text(json.dumps(dict(passed=True,expected_stops=3,checked_sha256=checked,guarded_sha256=guarded,branch_sha256=hashlib.sha256(branch.encode()).hexdigest(),harness_sha256=sha(h),scope='Actual flush branch with a fixed publication-phase input; checked five-byte read, one-shot/count guard, final-byte mismatch and unreadable guest page. No service or engine success substitution.'),indent=2)+'\n');print('PASS: actual checked flush branch; repeated flush, wrong byte and unreadable page rejected')
