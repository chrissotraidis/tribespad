#include "../port/core/guest_memory_checks.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
uint64_t base;
extern uint32_t tested_prior_phase(void);
extern int tested_font_ready(unsigned);
extern int tested_prefix_ready(void),tested_first_nop_ready(void),tested_second_nop_ready(void),tested_tail_ready(void),tested_display_ready(void),tested_calls_ready(void),tested_pending_ready(void);
static atomic_uint stop_readers,partial_reads[20][4],font_reads[2][4],font_pending;
static atomic_uint font_ack[4];
static void *reader(void *arg){
 unsigned index=(unsigned)(uintptr_t)arg;
 while(!atomic_load(&stop_readers)){
  uint32_t current=tribes_memory_code_revision(base),prior=tested_prior_phase();assert(prior==26||prior==28||prior==30||prior==32||prior==34||prior==36||prior==38||prior==40||prior==42||prior==44||prior==46||prior==48||prior==50||prior==52||prior==54||prior==56||prior==58||prior==60||prior==62||prior==64||prior==66);
  unsigned pending=atomic_load(&font_pending);
  if(pending){assert(!tested_font_ready(pending-1));atomic_fetch_add(&font_reads[pending-1][index],1);}
  atomic_store(&font_ack[index],pending);
  if(current==3){unsigned completed=tribes_memory_code_completed_revision(base);assert(completed==26||completed==28||completed==30||completed==32||completed==34||completed==36||completed==38||completed==40||completed==42||completed==44||completed==46||completed==48||completed==50||completed==52||completed==54||completed==56||completed==58||completed==60||completed==62||completed==64||completed==66);if(completed>=28)assert(tested_prefix_ready());if(completed>=30)assert(tested_first_nop_ready());if(completed>=32)assert(tested_second_nop_ready());if(completed>=34)assert(tested_tail_ready());if(completed>=36)assert(tested_display_ready());if(completed>=38)assert(tested_calls_ready());if(completed>=42)assert(tested_pending_ready());if(completed>=56)assert(tested_font_ready(0)&&tested_font_ready(1));if(completed<=64)atomic_fetch_add(&partial_reads[(completed-26)/2][index],1);}
 }
 return NULL;
}
int main(void){
 unsigned char *memory=mmap(NULL,0x480000,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;assert(tribes_memory_checks_configure(base,0x480000));
 TribesCodeWrite write={1024,4096,4,0,1};assert(tribes_memory_prepare_code_revision(base,&write,1));unsigned char expected[16]={0};uint32_t fonts[2]={544,544};memcpy(memory+8192,fonts,sizeof fonts);TribesPublishedCodeRange ranges[]={{1024,16,expected},{4096,16,expected},{8192,sizeof fonts,(unsigned char *)fonts}};assert(tribes_memory_publish_code(base,ranges,3));
 for(unsigned i=1;i<=13;i++){
  if(i>1){write.before=i-1;write.value=i;assert(tribes_memory_prepare_next_code_revision(base,&write,1));}
  assert(tribes_memory_apply_code_write(base,1024,memory+4096,4,i));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==i*2);
 }
 pthread_t workers[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(workers+i,NULL,reader,(void *)(uintptr_t)i));
 TribesCodeWrite prefix[]={{1024,4096,4,13,0x1004ae80},{1028,4100,1,0,0x5b}};assert(tribes_memory_prepare_next_code_revision(base,prefix,2));assert(tribes_memory_apply_code_write(base,1024,memory+4096,4,0x1004ae80));assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==26);
 for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[0]+i)<10000)sched_yield();
 assert(tribes_memory_apply_code_write(base,1028,memory+4100,1,0x5b));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==28);
 TribesCodeWrite nop[]={{1024,4104,4,0,0x90909090},{1028,4108,2,0,0x9090},{1032,4110,1,0,0x90}};
 assert(tribes_memory_prepare_next_code_revision(base,nop,3));assert(tribes_memory_apply_code_write(base,1024,memory+4104,4,0x90909090));
 assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==28);
 for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[1]+i)<10000)sched_yield();
 assert(tribes_memory_apply_code_write(base,1028,memory+4108,2,0x9090));assert(tribes_memory_apply_code_write(base,1032,memory+4110,1,0x90));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==30);
 TribesCodeWrite second[]={{1024,4101,2,0,0x9090},{1028,4103,1,0,0x90}};
 assert(tribes_memory_prepare_next_code_revision(base,second,2));assert(tribes_memory_apply_code_write(base,1024,memory+4101,2,0x9090));
 assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==30);
 for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[2]+i)<10000)sched_yield();
 assert(tribes_memory_apply_code_write(base,1028,memory+4103,1,0x90));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==32);
 TribesCodeWrite tail[]={{1024,4111,1,0,0x54},{1028,4100,1,0x5b,2}};
 assert(tribes_memory_prepare_next_code_revision(base,tail,2));assert(tribes_memory_apply_code_write(base,1024,memory+4111,1,0x54));assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==32);
 for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[3]+i)<10000)sched_yield();
 assert(tribes_memory_apply_code_write(base,1028,memory+4100,1,2));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==34);
 TribesCodeWrite display[]={{1024,4104,4,0x90909090,2732},{1028,4108,2,0x9090,2048}};
 assert(tribes_memory_prepare_next_code_revision(base,display,2));assert(tribes_memory_apply_code_write(base,1024,memory+4104,4,2732));assert(tribes_memory_code_revision(base)==3&&tribes_memory_code_completed_revision(base)==34);
 for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[4]+i)<10000)sched_yield();
 assert(tribes_memory_apply_code_write(base,1028,memory+4108,2,2048));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==36);
 TribesCodeWrite calls[]={{1024,4096,4,0x1004ae80,0x0fc448f2},{1028,4104,4,2732,0x0fc5016a}};
 assert(tribes_memory_prepare_next_code_revision(base,calls,2));assert(tribes_memory_apply_code_write(base,1024,memory+4096,4,0x0fc448f2));
 for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[5]+i)<10000)sched_yield();
 assert(tribes_memory_apply_code_write(base,1028,memory+4104,4,0x0fc5016a));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==38);
 TribesCodeWrite input[]={{1024,4096,4,0x0fc448f2,0x90909090},{1028,4104,4,0x0fc5016a,0x90909090}};
 assert(tribes_memory_prepare_next_code_revision(base,input,2));assert(tribes_memory_apply_code_write(base,1024,memory+4096,4,0x90909090));
 for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[6]+i)<10000)sched_yield();
 assert(tribes_memory_apply_code_write(base,1028,memory+4104,4,0x90909090));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==40);
 TribesPublishedCodeRange bank[34];for(unsigned i=0;i<34;i++)bank[i]=(TribesPublishedCodeRange){0x3c0010+i*72,10,expected};
 assert(tribes_memory_publish_pending_bank(base,bank,34));
 TribesCodeWrite pending[]={{1024,4096,4,0x90909090,0xe9},{1028,4104,4,0x90909090,0xcc}};
 assert(tribes_memory_prepare_next_code_revision(base,pending,2));assert(tribes_memory_apply_code_write(base,1024,memory+4096,4,0xe9));
 for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[7]+i)<10000)sched_yield();
 assert(tribes_memory_apply_code_write(base,1028,memory+4104,4,0xcc));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==42);
 TribesCodeWrite renderer={1024,4096,4,0xe9,0x0fb408b0};
 assert(tribes_memory_prepare_next_code_revision(base,&renderer,1));assert(tribes_memory_apply_code_write(base,1024,memory+4096,4,renderer.value));
 for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[8]+i)<10000)sched_yield();
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==44);
 TribesCodeWrite startup[]={{1024,4096,4,0x0fb408b0,0x90909090},{1028,4096,2,0x9090,0xd989}};
 assert(tribes_memory_prepare_next_code_revision(base,startup,2));assert(tribes_memory_apply_code_write(base,1024,memory+4096,4,startup[0].value));
 for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[9]+i)<10000)sched_yield();
 assert(tribes_memory_apply_code_write(base,1028,memory+4096,2,startup[1].value));assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==46);
 for(unsigned group=0;group<10;group++){
  if(group==7){TribesPublishedCodeRange renderer[26];for(unsigned j=0;j<26;j++)renderer[j]=(TribesPublishedCodeRange){0x3c0ea0+j*72,10,expected};assert(tribes_memory_publish_renderer_bank(base,renderer,26));}
  if(group==9){TribesPublishedCodeRange audio[7];for(unsigned j=0;j<7;j++)audio[j]=(TribesPublishedCodeRange){0x3c15f0+j*72,10,expected};assert(tribes_memory_publish_audio_bank(base,audio,7));}

  TribesCodeWrite redirect={1024,4096,4,group?group:0x9090d989,group+1};
  assert(tribes_memory_prepare_next_code_revision(base,&redirect,1));assert(tribes_memory_apply_code_write(base,1024,memory+4096,4,redirect.value));
  for(unsigned i=0;i<4;i++)while(atomic_load(partial_reads[10+group]+i)<10000)sched_yield();
  assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_completed_revision(base)==48+2*group);
  if(group==3)for(unsigned index=0;index<2;index++){
   TribesCodeWrite font={1024,8192+4*index,4,544,568};
   assert(tribes_memory_prepare_font_size_revision(base,index,&font));
   assert(tribes_memory_apply_code_write(base,1024,memory+font.address,4,568));
   atomic_store(&font_pending,index+1);
   for(unsigned i=0;i<4;i++)while(atomic_load(&font_reads[index][i])<10000)sched_yield();
   atomic_store(&font_pending,0);
   for(unsigned i=0;i<4;i++)while(atomic_load(&font_ack[i]))sched_yield();
   assert(tribes_memory_finish_code_revision(base)&&tested_font_ready(index));
   assert(tribes_memory_code_revision(base)==54);
  }
 }
 assert(tested_font_ready(0)&&tested_font_ready(1));
 atomic_store(&stop_readers,1);
 for(unsigned i=0;i<4;i++)assert(!pthread_join(workers[i],NULL));
 assert(tested_prior_phase()==66&&memory[4100]==2&&memory[4111]==0x54);puts("PASS: four actual readers retain prior routing across800000 partial-revision observations in revisions14/15/16/17/18/19/20/21/22/23/24/25/26/27/28/29/30/31/32/33 plus80000 pending font observations and publication completion");return 0;
}
