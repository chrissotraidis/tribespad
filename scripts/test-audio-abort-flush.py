#!/usr/bin/env python3
"""Exercise the actual finite audio-abort flush handler and corrupt each bank entry."""
import hashlib,json,pathlib,re,struct,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
build=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
out=R/'generated/integration-review/audio-fallback/flush-test';out.mkdir(parents=True,exist_ok=True)
source=(R/'tests/qol_native_bootstrap.c').read_text()
handler=re.search(r'^static uint32_t qol_audio_abort_flush\(.*?^}',source,re.M|re.S)[0]
data=(build/'published-code.bin').read_bytes();count=struct.unpack_from('<I',data)[0];offset=4;rows=[]
for _ in range(count):
 address,size=struct.unpack_from('<II',data,offset);offset+=8
 if 0x3c0000<=address<0x3d0000:rows.append((address,data[offset:offset+size]))
 offset+=size
assert len(rows)==15
c=['#include <assert.h>','#include <stdint.h>','#include <stdio.h>','#include <stdlib.h>','#include <string.h>','#include <sys/mman.h>','#include <sys/wait.h>','#include <unistd.h>',
 '#include "guest_memory_checks.h"','#include "pending_hook_publication.inc"','#include "renderer_hook_publication.inc"',
 '#define TB_PATCH_ADDRESS 0x3c0000u','static uint64_t base;static unsigned phase=62,stage=2,engine_entered=1,renderer_hook_write_count=42,renderer_hook_flush_count=19;',
 'static struct {uint32_t _esp;} state={0x2f0000};',
 'uint32_t tribes_memory_code_revision(uint64_t b){assert(b==base);return phase;}',
 'static int stack_readable(const void *s,unsigned n){return s==&state&&state._esp==0x2f0000&&n==60;}']
for i,(_,b) in enumerate(rows):c.append('static const unsigned char bytes%d[]={%s};'%(i,','.join(str(x) for x in b)))
c.append('static const TribesPublishedCodeRange publication_ranges[]={'+','.join('{0x%x,%d,bytes%d}'%(a,len(b),i) for i,(a,b) in enumerate(rows))+'};static unsigned publication_count=15;')
c.append(handler)
c.append('''static void put(unsigned a,unsigned v){memcpy((void *)(uintptr_t)(base+a),&v,4);}
static void deny(unsigned kind,unsigned index){
 pid_t child=fork();assert(child>=0);if(!child){
  uint32_t process=UINT32_MAX,address=TB_PATCH_ADDRESS,bytes=65536;
  if(kind<3){const TribesPublishedCodeRange *r=(kind==0?publication_ranges:kind==1?pending_bank_ranges:renderer_bank_ranges)+index;*(unsigned char *)(uintptr_t)(base+r->address)^=1;}
  else switch(index){
   case 0:process=1;break;case 1:address++;break;case 2:bytes--;break;
   case 3:put(0x101d6e20,1);break;case 4:put(0x101d6e1c,1);break;
   case 5:put(0x101d6e00,0x3d0000);break;case 6:put(0x3c0004,0x3d0000);break;
   case 7:put(state._esp,0);break;case 8:put(state._esp+36,0);break;case 9:put(state._esp+56,0);break;
   case 10:phase=3;break;case 11:phase=60;break;case 12:phase=64;break;
   case 13:renderer_hook_write_count--;break;case 14:renderer_hook_flush_count--;break;
   case 15:stage=1;break;case 16:engine_entered=0;break;case 17:state._esp=0;break;
  }
  qol_audio_abort_flush(process,address,bytes);_exit(0);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==6);
}
int main(void){
 void *memory=mmap(0,1ull<<32,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 for(unsigned group=0;group<3;group++){const TribesPublishedCodeRange *r=group==0?publication_ranges:group==1?pending_bank_ranges:renderer_bank_ranges;unsigned n=group==0?15:group==1?34:18;for(unsigned i=0;i<n;i++)memcpy((void *)(uintptr_t)(base+r[i].address),r[i].expected,r[i].size);}
 put(state._esp,0x1007202b);put(state._esp+36,0x10072609);put(state._esp+56,0x1005a31f);put(0x101d6e00,TB_PATCH_ADDRESS);
 assert(qol_audio_abort_flush(UINT32_MAX,TB_PATCH_ADDRESS,65536)==1&&phase==62);
 assert(qol_audio_abort_flush(UINT32_MAX,TB_PATCH_ADDRESS,65536)==1&&phase==62);
 for(unsigned group=0;group<3;group++)for(unsigned i=0;i<(group==0?15:group==1?34:18);i++)deny(group,i);
 for(unsigned i=0;i<18;i++)deny(3,i);
 puts("PASS: unchanged flush preserves revision; 67 altered entries and 18 invalid cleanup states rejected");return 0;
}''')
(out/'test.c').write_text('\n'.join(c)+'\n');exe=out/'test'
subprocess.run(['clang','-O2','-I',str(R/'port/core'),'-I',str(build),str(out/'test.c'),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True);(out/'stderr.log').write_text(run.stderr);assert not run.returncode,run.stderr
report=dict(passed=True,altered_entries_rejected=67,invalid_states_rejected=18,harness_sha256=hashlib.sha256(source.encode()).hexdigest(),binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),scope='Actual audio-abort flush handler; byte/transaction/caller validation and unchanged phase. Checked memory is exercised separately by full engine.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout,end='')
