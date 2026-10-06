#include "../port/core/guest_address_space.h"
#include "../port/core/guest_virtual_memory.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
extern void checked_write(void *,uint64_t);
static unsigned char *memory;
static uint64_t base;
static int fail_backend;
static int protect_backend(uint64_t b,uint32_t a,uint32_t n,const unsigned char *p){if(fail_backend){fail_backend=0;return 0;}return tribes_memory_protect_pages(b,a,n,p);}
static void query(uint32_t a,uint32_t out[7]){assert(tribes_virtual_query(base,a,0x100100,28)==28);memcpy(out,memory+0x100100,28);}
static void nonexecuting(uint32_t a){
 mach_vm_address_t address=(mach_vm_address_t)(base+a);mach_vm_size_t size=0;
 vm_region_basic_info_data_64_t info;mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=MACH_PORT_NULL;
 assert(mach_vm_region(mach_task_self(),&address,&size,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object)==KERN_SUCCESS);
 assert(address<=base+a&&address+size>base+a&&!(info.protection&VM_PROT_EXECUTE));
 if(object!=MACH_PORT_NULL)assert(mach_port_deallocate(mach_task_self(),object)==KERN_SUCCESS);
}
static void forbidden_write(uint32_t a){pid_t child=fork();assert(child>=0);if(!child){struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);checked_write(memory+a,0);_exit(99);}int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);}
int main(void){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_SHARED|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x100000,65536,PROT_READ|PROT_WRITE)&&!mprotect(memory+0x400000,65536,PROT_READ|PROT_WRITE));
 assert(tribes_address_space_begin(base));
 assert(tribes_address_space_declare(base,0x100000,65536,0x100000,4,0x1000,4,0x20000));
 assert(tribes_address_space_declare(base,0x400000,65536,0x400000,4,0x1000,4,0x1000000));assert(tribes_address_space_seal(base));
 const uint32_t ranges[][2]={{0x400000,4096}};
 assert(!tribes_address_space_enable_patch_capture(base,0x3c0000,ranges,1));
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
 const TribesCheckedMemoryBackend backend={tribes_memory_initialize_static,protect_backend};assert(tribes_address_space_enable_checked(base,&backend));
 assert(!tribes_virtual_protect(base,0x400000,1,0x40,0x100080)&&tribes_get_last_error()==50);
 assert(!tribes_address_space_enable_patch_capture(base,0x400000,ranges,1));
 assert(tribes_address_space_enable_patch_capture(base,0x3c0000,ranges,1));
 assert(!tribes_address_space_enable_patch_capture(base,0x3c0000,ranges,1));
 uint32_t fields[7];query(0x3c0000,fields);assert(fields[4]==0x10000); /* setup does not reserve it */
 assert(!tribes_virtual_alloc(base,0x3b0000,65536,0x3000,0x40)&&tribes_get_last_error()==50);
 fail_backend=1;assert(!tribes_virtual_alloc(base,0x3c0000,65536,0x3000,0x40)&&tribes_get_last_error()==5);
 query(0x3c0000,fields);assert(fields[4]==0x10000);nonexecuting(0x3c0000);
 assert(tribes_virtual_alloc(base,0x3c0000,65536,0x3000,0x40)==0x3c0000);
 query(0x3c0000,fields);assert(fields[1]==0x3c0000&&fields[2]==0x40&&fields[3]==65536&&fields[4]==0x1000&&fields[5]==0x40&&fields[6]==0x20000);
 for(unsigned i=0;i<65536;i++)assert(!memory[0x3c0000+i]);
 checked_write(memory+0x3c0080,UINT64_C(0x1234567812345678));
 assert(!tribes_virtual_alloc(base,0x3c0000,65536,0x3000,0x40)&&tribes_get_last_error()==487);
 assert(tribes_virtual_protect(base,0x3c0080,8,0x20,0x100080));uint32_t old;memcpy(&old,memory+0x100080,4);assert(old==0x40);
 query(0x3c0000,fields);assert(fields[3]==4096&&fields[5]==0x20);query(0x3c1000,fields);assert(fields[5]==0x40);
 unsigned char snapshot[65536];memcpy(snapshot,memory+0x3c0000,65536);forbidden_write(0x3c0080);assert(!memcmp(snapshot,memory+0x3c0000,65536));
 checked_write(memory+0x3c1000,0xa5);nonexecuting(0x3c0000);nonexecuting(0x3c1000);
 assert(!tribes_virtual_protect(base,0x3c0000,4096,0x40,0x3c0080)&&tribes_get_last_error()==998);
 assert(tribes_virtual_protect(base,0x3c0000,4096,0x40,0x100080));memcpy(&old,memory+0x100080,4);assert(old==0x20);
 assert(tribes_virtual_protect(base,0x3c0000,4096,0x40,0x3c0080));memcpy(&old,memory+0x3c0080,4);assert(old==0x40);
 assert(tribes_virtual_protect(base,0x400000,6,0x40,0x100080));checked_write(memory+0x400000,0x90);nonexecuting(0x400000);
 assert(!tribes_virtual_protect(base,0x401000,1,0x40,0x100080)&&tribes_get_last_error()==50);
 assert(!tribes_virtual_protect(base,0x100000,1,0x20,0x100080)&&tribes_get_last_error()==50);
 assert(tribes_virtual_protect(base,0x400000,6,0x20,0x100080));forbidden_write(0x400000);nonexecuting(0x400000);
 puts("PASS: explicit patch-capture allocation/query/protection, failed allocation preservation, page whitelist, independent subpages and checked write faults; Mach confirms host mappings never executable");return 0;
}
