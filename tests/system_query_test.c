#include "../port/core/guest_system_query.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
#ifdef CHECKED_TEST
extern void system_query_guest_count(void *), system_query_guest_vm(void *), system_query_wrong_abi(void), system_query_unreviewed(void);
#endif
static unsigned char *memory;
static uint64_t base;
static uint32_t read32(uint32_t at){uint32_t n;memcpy(&n,memory+at,4);return n;}
static uint32_t call(uint32_t cl,uint32_t output,uint32_t capacity,uint32_t len){
 uint32_t args[]={cl,output,capacity,len};_cpu cpu;memset(&cpu,0xa9,sizeof cpu);
 cpu._pointer_offset=base;cpu._esp=0xc003;memcpy(memory+cpu._esp,args,sizeof args);_cpu expected=cpu;
 tribes_set_last_error(0x12345678);tribes_enter(&cpu,1);expected._eax=cpu._eax;expected._esp+=sizeof args;
 assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(memory+0xc003,args,sizeof args)&&tribes_get_last_error()==0x12345678);return cpu._eax;
}
static unsigned snapshot(uint64_t result[32][3]){
 natural_t count;processor_info_array_t data;mach_msg_type_number_t words;mach_port_t host=mach_host_self();
 assert(host_processor_info(host,PROCESSOR_CPU_LOAD_INFO,&count,&data,&words)==KERN_SUCCESS);
 assert(mach_port_deallocate(mach_task_self(),host)==KERN_SUCCESS);assert(count&&words>=count*CPU_STATE_MAX);
 unsigned n=count>32?32:count;uint64_t rate=(uint64_t)sysconf(_SC_CLK_TCK);assert(rate>0);
 for(unsigned i=0;i<n;i++){
  const natural_t *t=(const natural_t *)data+i*CPU_STATE_MAX;
  result[i][0]=(uint64_t)t[CPU_STATE_IDLE]*10000000/rate;
  result[i][1]=((uint64_t)t[CPU_STATE_IDLE]+t[CPU_STATE_SYSTEM])*10000000/rate;
  result[i][2]=((uint64_t)t[CPU_STATE_USER]+t[CPU_STATE_NICE])*10000000/rate;
 }
 assert(vm_deallocate(mach_task_self(),(vm_address_t)data,words*sizeof(integer_t))==KERN_SUCCESS);return n;
}
static void *worker(void *arg){
 uint32_t output=0x40000+(uint32_t)(uintptr_t)arg*0x2000;
 for(unsigned i=0;i<8;i++){
  assert(!tribes_nt_query_system_information(base,8,output,1536,output+0x1800));
  uint32_t size=read32(output+0x1800);assert(size>=48&&size<=1536&&size%48==0);
 }
 return NULL;
}
#if defined(CHECKED_TEST) && !defined(SIMULATOR_TEST)
static void rejected(unsigned mode){
 unsigned char saved[4096];memcpy(saved,memory+0x18000,sizeof saved);pid_t child=fork();assert(child>=0);
 if(!child){
  if(mode==0){assert(tribes_memory_permissions_set(0x18000,4096,1));call(8,0x18000,48,0);}
  if(mode==1){assert(tribes_memory_permissions_set(0x19000,4096,1));call(8,0x18ff0,48,0);}
  if(mode==2){assert(tribes_memory_permissions_set(0x18000,4096,1));call(8,0,0,0x18000);}
  if(mode==3)call(999,0,0,0);
  if(mode==4)system_query_guest_count(memory+0x18000);
  if(mode==5)system_query_guest_vm(memory+0x18000);
  if(mode==6)system_query_wrong_abi();
  if(mode==7)system_query_unreviewed();
  _exit(90);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 assert(!memcmp(saved,memory+0x18000,sizeof saved));
}
#endif
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 for(unsigned i=0;i<48;i++){assert(call(8,0x10000,i,0x1001)==0xc0000004);assert(!read32(0x1001));}
 assert(call(8,0,48,0x1001)==0xc0000005);assert(call(8,0xfffffff0,48,0x1001)==0xc0000005);
 assert(call(8,0x10000,48,0xfffffffe)==0xc0000005);
 uint32_t sizes[]={48,49,95,96,384,1536,1600,UINT32_MAX};unsigned comparisons=0;
 for(unsigned i=0;i<sizeof sizes/sizeof *sizes;i++){
  uint64_t before[32][3],after[32][3];unsigned n=snapshot(before);
  memset(memory+0x10000,0xa5,0x2000);assert(!call(8,0x10001,sizes[i],0x1001));assert(snapshot(after)==n);
  unsigned expected=sizes[i]/48;if(expected>n)expected=n;assert(read32(0x1001)==expected*48);
  assert(memory[0x10000]==0xa5&&memory[0x10001+expected*48]==0xa5);
  for(unsigned j=0;j<expected;j++){
   uint64_t row[6];memcpy(row,memory+0x10001+j*48,48);
   for(unsigned k=0;k<3;k++){assert(before[j][k]<=row[k]&&row[k]<=after[j][k]);comparisons++;}
   assert(row[1]>=row[0]&&!row[3]&&!row[4]&&!row[5]);
  }
 }
 assert(!call(8,0x10000,48,0));
 pthread_t threads[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
#if defined(CHECKED_TEST) && !defined(SIMULATOR_TEST)
 for(unsigned i=0;i<8;i++)rejected(i);
#endif
 printf("PASS: %u live counter bounds, 48 short buffers, partial/full records, emitted ABI/CPU/LastError, 32 concurrent queries; protected-output checks when enabled\n",comparisons);return 0;
}
