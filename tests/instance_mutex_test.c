#include "../port/core/native_handles.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/llasm-support/llasm_cpu.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
extern void tribes_enter(_cpu *,uint32_t);
static unsigned char *memory;static uint64_t base;
static uint32_t call(unsigned target,const uint32_t *args,size_t count){
 _cpu cpu;memset(&cpu,0xa9,sizeof cpu);cpu._pointer_offset=base;cpu._esp=0x18003;memcpy(memory+cpu._esp,args,count*4);_cpu expected=cpu;
 tribes_enter(&cpu,target);expected._eax=cpu._eax;expected._esp+=(uint32_t)count*4;
 assert(!memcmp(&cpu,&expected,sizeof cpu)&&!memcmp(memory+0x18003,args,count*4));return cpu._eax;
}
static void *opener(void *unused){(void)unused;for(unsigned i=0;i<100;i++){uint32_t h=tribes_create_mutex(base,0,1,0x11000);assert(h&&tribes_get_last_error()==183);assert(tribes_close_handle(h));}return NULL;}
int main(int argc,char **argv){
 assert(argc==3);assert(tribes_named_mutex_configure(argv[1]));assert(tribes_named_mutex_configure(argv[1]));
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;
 assert(!mprotect(memory+0x10000,0x10000,PROT_READ|PROT_WRITE));strcpy((char *)memory+0x11000,"Tribes2Game");
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));tribes_memory_external_configure(base,UINT64_C(0x100000000));
#ifdef CHECKED_TEST
 tribes_memory_external_enable();
#endif
 uint32_t args[]={0,1,0x11000};
 if(!strcmp(argv[2],"reject-name"))strcpy((char *)memory+0x11000,"Different");
 if(!strcmp(argv[2],"reject-security"))args[0]=0x12000;
 if(!strcmp(argv[2],"reject-unowned"))args[1]=0;
 if(!strcmp(argv[2],"reject-unnamed"))args[2]=0;
 if(!strcmp(argv[2],"reject-memory"))assert(tribes_memory_permissions_set(0x11000,4096,0));
 tribes_set_last_error(183);uint32_t h=call(1,args,3),error=tribes_get_last_error();assert(h);
 if(!strcmp(argv[2],"hold")){printf("%u\n",error);fflush(stdout);assert(getchar()=='\n');assert(call(2,&h,1));return 0;}
 if(!strcmp(argv[2],"reject-wait")){tribes_wait_single_object(h,0);abort();}
 if(!strcmp(argv[2],"reject-duplicate")){tribes_duplicate_handle(base,UINT32_MAX,h,UINT32_MAX,0x12000,0,0,2);abort();}
 assert(!strcmp(argv[2],"test")&&error==0);
 assert(tribes_fd_borrow(h)==-1&&tribes_get_last_error()==6);uint32_t status;assert(tribes_fd_borrow_nt(h,&status)==-1&&status==0xc0000024u);
 assert(!tribes_get_file_type(h)&&tribes_get_last_error()==6);
 uint32_t second=call(1,args,3);assert(second&&second!=h&&tribes_get_last_error()==183);
 assert(call(2,&h,1));uint32_t third=call(1,args,3);assert(third&&tribes_get_last_error()==183);assert(call(2,&second,1));
 pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,opener,NULL));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 assert(call(2,&third,1));assert(!call(2,&third,1)&&tribes_get_last_error()==6);
 h=call(1,args,3);assert(h&&tribes_get_last_error()==0);assert(call(2,&h,1));
 assert(!tribes_create_mutex(base,0,1,UINT32_MAX-3)&&tribes_get_last_error()==87);
 puts("PASS: emitted CreateMutexA/CloseHandle ABI, real named lifetime, reopen and final close, 400 concurrent opens, typed handles and invalid inputs");return 0;
}
