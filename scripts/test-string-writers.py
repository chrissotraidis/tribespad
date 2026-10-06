#!/usr/bin/env python3
"""Exercise exact seventh-patch helpers with checked ordinary-store fallback."""
import argparse,hashlib,json,pathlib,re,subprocess
from checked_support import compile_support
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('inputs',type=pathlib.Path);a=p.parse_args();root=a.inputs.resolve();meta=json.loads((root/'string-patch-inputs.json').read_text());inc=root/'string_publication.inc';assert sha(inc)==meta['include_sha256']
out=R/'generated/integration-review/string-writers';out.mkdir(exist_ok=True)
harness=R/'tests/qol_native_bootstrap.c';text=harness.read_text();functions=[]
for name in ['qol_string_code_write','qol_string_shared_write','qol_string_vector_write']:
 m=re.search(r'^void '+name+r'\([^\n]+\{\n.*?^}',text,re.M|re.S);assert m;functions.append(m[0])
common='#include "llasm_cpu.h"\n#include "guest_memory_checks.h"\n#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n#include <stdint.h>\n#include "string_publication.inc"\n'
globals='uint64_t base;_cpu state;int engine_entered=1,stage=2;unsigned http_write_count=17,http_flush_count=1,string_write_count;\n'
helpers=out/'helpers.c';helpers.write_text(common+globals+'\n'.join(functions)+'\n')
objects,build_sha=compile_support([helpers],out/'checked',['-DPTROFS_64BIT','-I',str(R/'port/llasm-support'),'-I',str(R/'port/core'),'-I',str(root)])
source=out/'test.c';source.write_text(common+r'''
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern uint64_t base;extern _cpu state;extern unsigned string_write_count;
extern void qol_string_code_write(_cpu*,uint32_t,const void*,uint32_t,uint64_t);
extern void qol_string_shared_write(_cpu*,uint32_t,void*,uint32_t,uint64_t);
extern void qol_string_vector_write(_cpu*,uint32_t,uint32_t,uint32_t,uint32_t);
static unsigned faults;
static void fault(unsigned kind){
 unsigned char *memory=(void *)(uintptr_t)base;unsigned char before[32];uint32_t destination=kind==1?string_code_writes[string_write_count].address:kind==2?0x200000:0x427570;memcpy(before,memory+destination,32);
 pid_t child=fork();assert(child>=0);
 if(!child){
  if(kind==0)qol_string_shared_write(&state,0x100fbd10,memory+0x42757f,4,0);
  else if(kind==1){const TribesCodeWrite *w=string_code_writes+string_write_count;qol_string_shared_write(&state,w->pc,memory+w->address,w->size,w->value^1);}
  else {assert(tribes_memory_permissions_set(0x200000,4096,1));qol_string_shared_write(&state,0x100fbcf3,memory+0x200000,4,0);}
  _exit(90);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 assert(!memcmp(before,memory+destination,32));faults++;
}
static void ordinary(void){
 unsigned char *memory=(void *)(uintptr_t)base;_cpu saved=state;
 for(unsigned i=0;i<4;i++){
  uint32_t pc=(uint32_t[]){0x100fbcdd,0x100fbcf3,0x100fbd10,0x100fbd12}[i];unsigned size=i?4:1;
  memset(memory+0x200000,0xa5,8);qol_string_shared_write(&state,pc,memory+0x200001,size,0x12345678);
  uint64_t expected=0x12345678;assert(!memcmp(memory+0x200001,&expected,size)&&memory[0x200000]==0xa5&&memory[0x200001+size]==0xa5&&!memcmp(&saved,&state,sizeof state));
 }
}
int main(void){
 unsigned char *memory=mmap(0,0x11000000,PROT_READ|PROT_WRITE,MAP_ANON|MAP_SHARED,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;state._pointer_offset=base;
 /* First preimages initialize the source image, including overlap chains. */
 for(unsigned i=114;i;i--){const TribesCodeWrite *w=string_code_writes+i-1;memcpy(memory+w->address,&w->before,w->size);}
 assert(tribes_memory_checks_configure(base,0x11000000));
 TribesCodeWrite noop=string_code_writes[0];noop.value=noop.before;
 assert(tribes_memory_prepare_code_revision(base,&noop,1));
 TribesPublishedCodeRange ranges[]={{0x400000,0x400000,memory+0x400000},{0x10000000,0x200000,memory+0x10000000}};
 /* Publication requires host-owned copies of expected image bytes. */
 for(unsigned i=0;i<2;i++){unsigned char *copy=malloc(ranges[i].size);assert(copy);memcpy(copy,ranges[i].expected,ranges[i].size);ranges[i].expected=copy;}
 assert(tribes_memory_publish_code(base,ranges,2));
 for(unsigned i=0;i<6;i++){if(i)assert(tribes_memory_prepare_next_code_revision(base,&noop,1));assert(tribes_memory_apply_code_write(base,noop.pc,memory+noop.address,noop.size,noop.value));assert(tribes_memory_finish_code_revision(base));}
 assert(tribes_memory_code_revision(base)==12);ordinary();fault(0);fault(2);
 unsigned vector_calls=0,shared_writes=0;
 while(string_write_count<114){
  const TribesCodeWrite *w=string_code_writes+string_write_count;_cpu saved=state;
  if(w->pc==0x10012272||w->pc==0x10012c53||w->pc==0x10012c5e){
   unsigned mode=w->pc==0x10012c5e?2:4;memcpy(state._xmm[0],&w->value,8);if(mode==4)memcpy(state._xmm[0]+8,&w[1].value,8);saved=state;
   qol_string_vector_write(&state,w->pc,w->address,0,mode);vector_calls++;
  }else if(w->pc>0x100f0000){fault(1);qol_string_shared_write(&state,w->pc,memory+w->address,w->size,w->value);shared_writes++;}
  else qol_string_code_write(&state,w->pc,memory+w->address,w->size,w->value);
  assert(!memcmp(&state,&saved,sizeof state)&&tribes_memory_code_revision(base)==3);
  ordinary();
 }
 assert(vector_calls==3&&shared_writes==16);fault(0);
 assert(tribes_memory_finish_code_revision(base)&&tribes_memory_code_revision(base)==14);ordinary();fault(0);
 printf("PASS:114 ordered writes,3 vector calls,16 shared patch stores,ordinary checked writes and%u rejected writes\n",faults);
}
''')
exe=out/'test';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-DPTROFS_64BIT','-I',str(R/'port/llasm-support'),'-I',str(R/'port/core'),'-I',str(root),str(source),*objects,str(R/'port/core/guest_memory_checks.c'),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr);assert run.returncode==0,(run.returncode,run.stderr[-2000:])
report=dict(passed=True,inputs_sha256=sha(root/'string-patch-inputs.json'),include_sha256=sha(inc),harness_sha256=sha(harness),script_sha256=sha(pathlib.Path(__file__).resolve()),checked_helpers_sha256=build_sha,binary_sha256=sha(exe),writes=114,shared_patch_writes=16,vector_calls=3,expected_faults=20,scope='Actual patch helper bodies compiled through memory checks. Exact values and CPU preservation, ordinary memset writes before/during/after publication, wrong planned values and unexpected published/read-only writes. Full native startup and external-call guarding validated separately.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout,end='')
