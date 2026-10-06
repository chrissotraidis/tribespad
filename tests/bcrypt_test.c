#include "../port/core/guest_bcrypt.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
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
extern void rng_guest_pointer(void *),rng_wrong_width(void *),rng_wrong_return(void *);
#endif
static unsigned char *memory;static uint64_t base;
static uint32_t call(uint32_t alg,uint32_t output,uint32_t length,uint32_t flags){
 uint32_t args[]={alg,output,length,flags};_cpu cpu;memset(&cpu,0xa9,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0xc003;memcpy(memory+cpu._esp,args,sizeof args);_cpu expected=cpu;
 tribes_set_last_error(0x12345678);tribes_enter(&cpu,1);expected._eax=cpu._eax;expected._esp+=sizeof args;
 assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(memory+0xc003,args,sizeof args)&&tribes_get_last_error()==0x12345678);return cpu._eax;
}
static void *worker(void *arg){
 uint32_t address=0x2000+0x1000*(uint32_t)(uintptr_t)arg;unsigned char before[128];memset(before,0,128);tribes_set_last_error(address);
 for(unsigned i=0;i<128;i++){assert(!tribes_bcrypt_gen_random(base,0,address,128,2));assert(memcmp(before,memory+address,128));memcpy(before,memory+address,128);assert(tribes_get_last_error()==address);}return NULL;
}
#ifdef CHECKED_TEST
static void rejected(unsigned mode){
 unsigned char saved[16384];memcpy(saved,memory+0x4000,sizeof saved);fflush(stdout);pid_t child=fork();assert(child>=0);
 if(!child){
  if(mode==0){assert(tribes_memory_permissions_set(0x6000,4096,1));tribes_bcrypt_gen_random(base,0,0x5ff0,128,2);}
  else if(mode==1)tribes_bcrypt_gen_random(base,0x81,0x6000,128,0);
  else if(mode==2)tribes_bcrypt_gen_random(base,0,0x6000,128,4);
  else if(mode==3)rng_guest_pointer(memory+0x6000);
  else {unsigned char host[128];if(mode==4)rng_wrong_width(host);else rng_wrong_return(host);}
  _exit(90);
 }
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);assert(!memcmp(saved,memory+0x4000,sizeof saved));
}
#endif
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;assert(!mprotect(memory,65536,PROT_READ|PROT_WRITE));
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 assert(call(0,0,0,0)==0xc0000008);assert(call(0,0x6001,128,1)==0xc0000008);assert(call(0,0,128,2)==0xc000000d);assert(call(0,0xfffffff0,32,2)==0xc000000d);assert(!call(0,0x6001,0,2));
 unsigned lengths[]={1,7,128,256,4097,8201};unsigned char old[8201];
 for(unsigned flags=2;flags<=3;flags++)for(unsigned i=0;i<sizeof lengths/sizeof lengths[0];i++){
  unsigned n=lengths[i];memset(memory+0x6000,0x5a,n+2);assert(!call(0,0x6001,n,flags));assert(memory[0x6000]==0x5a&&memory[0x6001+n]==0x5a);
  memcpy(old,memory+0x6001,n);if(n>=128){assert(!call(0,0x6001,n,flags));assert(memcmp(old,memory+0x6001,n));}
 }
 pthread_t threads[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
#ifdef CHECKED_TEST
 for(unsigned mode=0;mode<6;mode++)rejected(mode);
#endif
 puts("PASS: real RNG via emitted ABI, status/bounds/zero-size/flags, CPU and LastError preservation, 512 concurrent calls; guarded output and host ABI rejection when enabled");return 0;
}
