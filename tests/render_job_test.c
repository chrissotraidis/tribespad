#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
extern uint64_t mission_fixture_init(void);
extern uint32_t tribes_render_job_waiters(void);
extern void test_begin(_cpu *),test_complete(_cpu *),test_wait(_cpu *),test_finish(_cpu *);
extern void tribes_render_job_step(_cpu *,uint32_t);
void emitted_store(uint32_t *p,uint32_t v){*p=v;}
static unsigned char *memory;
static _cpu render_cpu,worker_cpu;
static atomic_int launched,allow_completion;
static unsigned payload,iteration;
static int bad_job;
static void invoke(_cpu *c,void (*f)(_cpu *)){
 uint32_t sp=c->_esp;memset(memory+sp-16,0x73,48);_cpu before=*c;
 if(f==test_complete){uint32_t call[]={0x10076526,0x1046e410,0};if(bad_job)call[1]+=4;memcpy(memory+sp,call,sizeof call);}
 unsigned char tail[32];memcpy(tail,memory+sp,32);f(c);before._esp=sp-4;
 assert(!memcmp(c,&before,sizeof before)&&!memcmp(tail,memory+sp,32));
 uint32_t lock;memcpy(&lock,memory+sp-4,4);assert(lock==0x1046e3dc);c->_esp=sp;
}
static void *complete(void *unused){
 (void)unused;atomic_store(&launched,1);while(!atomic_load(&allow_completion)){}
 if(!(iteration&1))while(!tribes_render_job_waiters()){}
 payload=iteration;invoke(&worker_cpu,test_complete);return NULL;
}
static void *wrong_wait(void *unused){(void)unused;invoke(&worker_cpu,test_wait);return NULL;}
int main(int argc,char **argv){
 uint64_t base=mission_fixture_init();memory=(void *)(uintptr_t)base;
#ifdef CHECKED
 tribes_memory_external_configure(base,0x11000000);tribes_memory_external_enable();
#endif
 memset(&render_cpu,0xa5,sizeof render_cpu);render_cpu._pointer_offset=base;render_cpu._esp=0x200100;
 worker_cpu=render_cpu;worker_cpu._esp=0x210100;
 uint32_t work[]={0x10043960,0x100439b0};memcpy(memory+0x1046e450,work,sizeof work);
 if(argc>1){
  const char *f=argv[1];
  if(!strcmp(f,"bad-phase")){render_cpu._pointer_offset+=4096;tribes_render_job_step(&render_cpu,0);}
  if(!strcmp(f,"bad-step")){tribes_render_job_step(&render_cpu,4);}
  if(!strcmp(f,"readonly")){assert(tribes_memory_permissions_set(0x1046e000,4096,1));invoke(&render_cpu,test_begin);}
  if(!strcmp(f,"dirty")){memory[0x1046e3dc]=7;invoke(&render_cpu,test_begin);}
  if(!strcmp(f,"early-complete")){invoke(&worker_cpu,test_complete);}
  if(!strcmp(f,"bad-stack")){tribes_render_job_step(&render_cpu,0);}
  invoke(&render_cpu,test_begin);
  if(!strcmp(f,"repeat-begin"))invoke(&render_cpu,test_begin);
  if(!strcmp(f,"early-finish"))invoke(&render_cpu,test_finish);
  if(!strcmp(f,"wrong-wait")){pthread_t t;assert(!pthread_create(&t,NULL,wrong_wait,NULL));pthread_join(t,NULL);}
  if(!strcmp(f,"bad-callback")){memory[0x1046e454]=0;invoke(&worker_cpu,test_complete);}
  if(!strcmp(f,"bad-job")){bad_job=1;invoke(&worker_cpu,test_complete);}
  invoke(&worker_cpu,test_complete);
  if(!strcmp(f,"duplicate-complete"))invoke(&worker_cpu,test_complete);
  if(!strcmp(f,"modified")){memory[0x1046e3dc]=1;invoke(&render_cpu,test_wait);}
  return 99;
 }
 for(iteration=1;iteration<=200;iteration++){
  invoke(&render_cpu,test_begin);payload=0;pthread_t t;atomic_store(&launched,0);atomic_store(&allow_completion,0);
  assert(!pthread_create(&t,NULL,complete,NULL));while(!atomic_load(&launched)){}
  if(iteration&1){atomic_store(&allow_completion,1);assert(!pthread_join(t,NULL));invoke(&render_cpu,test_wait);}
  else{
   /* Worker observes an actual blocked waiter before publishing completion. */
   atomic_store(&allow_completion,1);invoke(&render_cpu,test_wait);assert(!pthread_join(t,NULL));
  }
  assert(payload==iteration);invoke(&render_cpu,test_finish);uint32_t word;memcpy(&word,memory+0x1046e3dc,4);assert(word==0);
 }
 puts("PASS: 200 emitted original render-fence cycles, callback completion and payload visibility, native concurrent wait, full CPU/live-stack preservation");return 0;
}
