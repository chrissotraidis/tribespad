#include "../port/core/guest_modules.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;static uint64_t base;
static uint32_t word(uint32_t p){uint32_t n;memcpy(&n,memory+p,4);return n;}
static void wide(uint32_t p,const char *s){for(unsigned n=0;;n++){uint16_t c=(unsigned char)s[n];memcpy(memory+p+2*n,&c,2);if(!c)break;}}
static uint32_t call(unsigned slot,unsigned target,const uint32_t *args,unsigned n,uint32_t error){
 _cpu c;memset(&c,0xa5,sizeof c);c._pointer_offset=base;c._esp=0x10003+slot*4096;uint32_t sp=c._esp;
 memset(memory+sp-68,0x71,64);memset(memory+sp+4*n,0x71,64);memcpy(memory+sp,args,4*n);_cpu before=c;
 tribes_set_last_error(12345);tribes_enter(&c,target);before._eax=c._eax;before._esp+=4*n;
 assert(!memcmp(&before,&c,sizeof c)&&!memcmp(memory+sp,args,4*n));
 for(unsigned i=0;i<64;i++)assert(memory[sp-68+i]==0x71&&memory[sp+4*n+i]==0x71);
 assert(tribes_get_last_error()==error);return c._eax;
}
static void ex(unsigned slot,unsigned target,uint32_t flags,uint32_t pointer,uint32_t expected,uint32_t error){
 uint32_t out=0x1001+slot*64,args[]={flags,pointer,out};memset(memory+out-1,0x79,6);
 assert(call(slot,target,args,3,error)==(expected!=0));assert(word(out)==expected);assert(memory[out-1]==0x79&&memory[out+4]==0x79);
}
static void release(unsigned slot,uint32_t image){assert(call(slot,3,&image,1,12345));}
static void *worker(void *arg){unsigned slot=(unsigned)(uintptr_t)arg;
 for(unsigned n=0;n<1000;n++){
  ex(slot,n%2+1,4,0x100759a0,0x10000000,12345);release(slot,0x10000000);
  ex(slot,1,0,0x3001,0x60010000,12345);release(slot,0x60010000);
 }
 return NULL;
}
static void fault(unsigned n){
 if(n==0)tribes_free_library(base,0x10000000);
 else if(n==1){uint16_t c=0x100;memcpy(memory+0x3001,&c,2);tribes_get_module_handle_ex_w(base,0,0x3001,0x1001);}
#ifdef CHECKED_TEST
 else if(n==2){assert(tribes_memory_permissions_set(0x80000,4096,1));tribes_get_module_handle_ex_w(base,4,0x100759a0,0x80000);}
 else if(n==3){assert(tribes_memory_permissions_set(0x80000,4096,1));tribes_get_module_handle_ex_a(base,4,0x100759a0,0x7fffe);}
 else if(n==4){assert(tribes_memory_permissions_set(0x80000,4096,0));tribes_get_module_handle_ex_w(base,0,0x80000,0x1001);}
 else if(n==5){assert(tribes_memory_permissions_set(0x80000,4096,0));tribes_get_module_handle_ex_a(base,0,0x80000,0x1001);}
#endif
 _exit(99);
}
#ifndef TSAN_TEST
static void reject(unsigned n){
 memset(memory+0x7fff0,0x79,32);pid_t child=fork();assert(child>=0);
 if(!child){struct rlimit lim={0,0};setrlimit(RLIMIT_CORE,&lim);fault(n);}
 int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 for(unsigned i=0;i<32;i++)assert(memory[0x7fff0+i]==0x79);
}
#endif
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));base=(uintptr_t)memory;
#ifdef CHECKED_TEST
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));tribes_memory_external_enable();
#endif
 assert(tribes_module_register_image(base,0x400000,0x63a000,"C:\\TribesPad\\Game\\Tribes2.exe",1));
 assert(tribes_module_register_image(base,0x10000000,0x48c000,"C:\\TribesPad\\Game\\IFC22.dll",0));
 assert(tribes_module_register_image(base,0xfffff000,0x1000,"C:\\Top.dll",0));
 assert(!tribes_module_register_image(base,0x1048bfff,2,"C:\\Overlap.dll",0)&&errno==EALREADY);
 assert(!tribes_module_register_image(base,0xfffff000,0x1001,"C:\\Overflow.dll",0)&&errno==EINVAL);
 assert(!tribes_module_register_image(base,0x80000000,0,"C:\\Zero.dll",0)&&errno==EINVAL);
 assert(!tribes_module_register(base,0x10000100,"C:\\Inside.dll",0)&&errno==EALREADY);
 TribesNativeExport exports[]={{"Example",1,0xfffd0000}};
 assert(!tribes_module_define_native(base,0x10000001,"C:\\InsideProvider.dll",exports,1)&&errno==EALREADY);
 assert(tribes_module_define_native(base,0x60010000,"C:\\Windows\\System32\\kernel32.dll",exports,1));
 assert(tribes_module_retain_native(base,0x60010000));wide(0x3001,"KeRnEl32");strcpy((char *)memory+0x4001,"iFc22");
 if(argc>1){fault((unsigned)strtoul(argv[1],NULL,10));return 99;}
 for(unsigned service=1;service<=2;service++){
  for(unsigned i=0;i<3;i++){uint32_t p=(uint32_t[]){0x10000000,0x100759a0,0x1048bfff}[i];ex(0,service,4,p,0x10000000,12345);release(0,0x10000000);}
  ex(0,service,6,0x100759a0,0x10000000,12345); /* No extra reference. */
  ex(0,service,4,0x0fffffff,0,126);ex(0,service,4,0x1048c000,0,126);
  ex(0,service,4,UINT32_MAX,0xfffff000,12345);release(0,0xfffff000);
  ex(0,service,0,0,0x400000,12345);release(0,0x400000);
  ex(0,service,4,0,0x400000,12345);release(0,0x400000);
  for(unsigned f=0;f<2;f++)ex(0,service,f?8:3,UINT32_MAX,0,87);
  uint32_t invalid[]={4,0x100759a0,0};assert(!call(0,service,invalid,3,87));invalid[2]=UINT32_MAX-2;assert(!call(0,service,invalid,3,87));
 }
 ex(0,2,0,0x4001,0x10000000,12345);release(0,0x10000000);
 wide(0x5001,"C:\\TRIBESPAD\\GAME\\IFC22.DLL");ex(0,1,0,0x5001,0x10000000,12345);release(0,0x10000000);
 wide(0x5001,"missing");ex(0,1,0,0x5001,0,126);wide(0x5001,"");ex(0,1,0,0x5001,0,126);
 ex(0,1,0,UINT32_MAX,0,87);memset(memory+0x5000,'A',8192);ex(0,2,0,0x5000,0,126);
 for(unsigned n=0;n<4096;n++){uint16_t c='A';memcpy(memory+0x5000+n*2,&c,2);}ex(0,1,0,0x5000,0,126);
 pthread_t threads[6];for(uintptr_t n=1;n<=6;n++)assert(!pthread_create(threads+n-1,NULL,worker,(void *)n));for(unsigned n=0;n<6;n++)assert(!pthread_join(threads[n],NULL));
 /* The initial native-provider reference survives every matched Ex/Free pair. */
 assert(tribes_get_module_handle_w(base,0x3001)==0x60010000);release(0,0x60010000);ex(0,1,0,0x3001,0,126);
 assert(tribes_module_retain_native(base,0x60010000));ex(0,1,2,0x3001,0x60010000,12345);release(0,0x60010000);ex(0,1,2,0x3001,0,126);
 assert(tribes_module_retain_native(base,0x60010000));ex(0,1,1,0x3001,0x60010000,12345);
 for(unsigned i=0;i<100;i++)release(0,0x60010000);assert(tribes_get_module_handle_w(base,0x3001)==0x60010000);assert(tribes_get_proc_address(base,0x60010000,1)==0xfffd0000);
 /* LoadLibrary must add a mapped-image reference just like GetModuleHandleEx. */
 assert(tribes_load_library(base,0x4001)==0x10000000);release(0,0x10000000);
#ifndef TSAN_TEST
 reject(0);reject(1);
#endif
#ifdef CHECKED_TEST
 for(unsigned i=2;i<6;i++)reject(i);
#endif
 ex(0,1,5,0x100759a0,0x10000000,12345);for(unsigned i=0;i<100;i++)release(0,0x10000000);
 puts("PASS: emitted A/W GetModuleHandleEx + FreeLibrary, 12000 concurrent retained address/name lookups and matched releases; authoritative half-open image extents including 4GiB end, overlap rejection, FROM_ADDRESS never dereferences code, main/name/ASCII UTF16 matching, unchanged references, permanent PIN, native-provider unloaded state, errors/NULL output, exact 4-byte output and full CPU/stack/LastError");return 0;
}
