#include "../port/core/guest_address_space.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#ifdef TB_CHECKED_SUBPAGES
extern void checked_write(void *,uint64_t);
extern uint64_t checked_read(void *);
#endif
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern void tbimp_VirtualProtect(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t address){if(address==1)return (void *)tbimp_VirtualProtect;if(address==0xfffff000)return (void *)tribes_return_to_host;abort();}
static unsigned char *memory;
static uint64_t base;
static _cpu cpu;
static uint32_t invoke(uint32_t address,uint32_t size,uint32_t protection,uint32_t output){
 uint32_t top=cpu._esp,args[]={address,size,protection,output};cpu._esp-=sizeof args;memcpy(memory+cpu._esp,args,sizeof args);_cpu before=cpu;
 tribes_enter(&cpu,1);before._eax=cpu._eax;before._esp=top;assert(!memcmp(&before,&cpu,sizeof cpu));assert(!memcmp(memory+top-sizeof args,args,sizeof args));return cpu._eax;
}
static void faults(uint32_t address,int write){
 pid_t child=fork();assert(child>=0);if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);volatile unsigned char *p=memory+address;if(write)*p=0x36;else {volatile unsigned char v=*p;(void)v;}_exit(99);}
 int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&(WTERMSIG(status)==SIGSEGV||WTERMSIG(status)==SIGBUS));
}
#ifdef TB_CHECKED_SUBPAGES
static void checked_fault(uint32_t address,int write){pid_t child=fork();assert(child>=0);if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);if(write)checked_write(memory+address,0x1234);else (void)checked_read(memory+address);_exit(99);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);}
#endif
static uint32_t protection(uint32_t address){assert(tribes_virtual_query(base,address,0x100100,28)==28);uint32_t fields[7];memcpy(fields,memory+0x100100,28);return fields[5];}
static void fails(uint32_t address,uint32_t size,uint32_t prot,uint32_t output,uint32_t error){
 uint32_t sentinel=0x87654321;memcpy(memory+0x100080,&sentinel,4);tribes_set_last_error(123);assert(!invoke(address,size,prot,output)&&tribes_get_last_error()==error);assert(!memcmp(memory+0x100080,&sentinel,4));
}
int main(void){
 uint32_t page=(uint32_t)sysconf(_SC_PAGESIZE);assert(page>=4096&&page<=65536);memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x100000,0x10000,PROT_READ|PROT_WRITE));assert(!mprotect(memory+0x400000,2*page,PROT_READ|PROT_WRITE));memset(&cpu,0xa5,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x10f003;
 fails(0x400000,page,2,0x100080,50);assert(tribes_address_space_begin(base));assert(tribes_address_space_declare(base,0x100000,0x10000,0x100000,4,0x1000,4,0x20000));
 assert(tribes_address_space_declare(base,0x400000,page,0x400000,4,0x1000,4,0x1000000));assert(tribes_address_space_declare(base,0x400000+page,page,0x400000+page,4,0x1000,4,0x1000000));assert(tribes_address_space_seal(base));
 #ifdef TB_CHECKED_SUBPAGES
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));const TribesCheckedMemoryBackend backend={tribes_memory_initialize_static,tribes_memory_protect_pages};assert(tribes_address_space_enable_checked(base,&backend));assert(!tribes_address_space_enable_checked(base,&backend));assert(!tribes_address_space_protect(base,0x400000,4096,2)&&tribes_get_last_error()==50);
 const uint32_t unowned[]={0x7fff0000,0xff044b83,0xfffffff8};
 for(unsigned i=0;i<sizeof unowned/sizeof *unowned;i++){checked_fault(unowned[i],0);checked_fault(unowned[i],1);faults(unowned[i],0);faults(unowned[i],1);}
#endif
 memory[0x400000]=0x72;memory[0x400000+page]=0x63;
 if(page>4096){
#ifdef TB_CHECKED_SUBPAGES
  assert(invoke(0x401000,128,2,0x100080));assert(protection(0x401000)==2&&protection(0x400000)==4);checked_fault(0x401000,1);checked_fault(0x400ffc,1);checked_write(memory+0x400080,0x8734);assert(invoke(0x401000,128,4,0x100080));checked_write(memory+0x401000,0x41);
#else
  fails(0x401000,128,2,0x100080,50);
#endif
  assert(protection(0x401000)==4);memory[0x401000]=0x41;memory[0x400000]=0x72;
 }
 fails(0x400000,page+1,2,0x100080,487);fails(0x800000,page,2,0x100080,487);fails(0x400000,0,2,0x100080,87);fails(0xfffff000,0x2000,2,0x100080,87);
 fails(0x400000,page,0x40,0x100080,50);fails(0x400000,page,0x102,0x100080,50);fails(0x60030000,page,2,0x100080,50);
 fails(0x400000,page,2,0,998);fails(0x400000,page,2,0x800000,998);fails(0x400000,page,2,0x400020,998);fails(0x400000,page,2,0x10ffff,998);
 tribes_set_last_error(123);assert(invoke(0x400001,page-1,2,0x100081));assert(tribes_get_last_error()==123);uint32_t old;memcpy(&old,memory+0x100081,4);assert(old==4&&protection(0x400000)==2&&memory[0x400000]==0x72);faults(0x400000,1);assert(protection(0x400000+page)==4);memory[0x400000+page]=0x64;
 /* A subpage no-op is representable when its neighbors already match. */
 assert(invoke(0x400000,1,2,0x100080));memcpy(&old,memory+0x100080,4);assert(old==2);faults(0x400000,1);
 assert(invoke(0x400000,page,1,0x100080));assert(protection(0x400000)==1);faults(0x400000,0);faults(0x400000,1);
 assert(invoke(0x400000,page,4,0x100080));assert(protection(0x400000)==4);memory[0x400000]=0x18;assert(memory[0x400000]==0x18);
 /* Output can lie in the target if the resulting protection remains writable. */
 assert(invoke(0x400000,page,4,0x400021));memcpy(&old,memory+0x400021,4);assert(old==4);
 #ifdef TB_CHECKED_SUBPAGES
 puts("PASS: checked 4 KiB VirtualProtect within a larger native page; independent neighbor writes; cross-page write rejection; upper unowned reads/writes rejected before native faults; old permissions, restoration, standard validation and whole-native-page faults");
#else
 puts("PASS: emitted VirtualProtect ABI and CPU preservation; actual native read-only/no-access faults; byte-range rounding and old protection; neighbor isolation; unchanged state/output on unsupported subpages, executable flags, dynamic owners, invalid ranges and output buffers");
#endif
 return 0;
}
