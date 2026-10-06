/* Finite AOT procedures exercise original stdcall frames on actual pthreads. */
#include "../../port/core/guest_threads.h"
#include "../../port/core/guest_fls.h"
#include "../../port/core/native_fls.h"
#include "../../port/core/guest_heap.h"
#include "../../port/core/guest_teb.h"
#include "../../port/core/guest_virtual_memory.h"
#include "../../port/core/native_handles.h"
#include "../../port/llasm-support/tribes_absolute_memory.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <setjmp.h>
extern void loc_1011C79B(void),loc_1011C7AD(void);
void tribes_set_dispatch_target(_cpu *c,uint32_t pc){(void)c;assert(pc==0xfffff000);}
extern void fixture_start(void),fixture_tls_a(void),fixture_dll_a(void),fixture_tls_b(void),fixture_dll_b(void),fixture_fls(void);
extern void tbimp_QueueUserWorkItem(void),tbimp_CreateThread(void),tbimp_ResumeThread(void),tbimp_GetExitCodeThread(void),tbimp_GetThreadId(void),tbimp_ExitThread(void),tbimp_FreeLibraryAndExitThread(void),tribes_return_to_host(void);
void *tribes_lookup(uint32_t pc){switch(pc){
 case 1:return (void *)tbimp_CreateThread;case 2:return (void *)tbimp_ResumeThread;case 3:return (void *)tbimp_GetExitCodeThread;case 4:return (void *)tbimp_GetThreadId;
 case 5:return (void *)tbimp_ExitThread;case 6:return (void *)tbimp_FreeLibraryAndExitThread;case 7:return (void *)tbimp_QueueUserWorkItem;
 case 0x100:return (void *)fixture_tls_a;case 0x101:return (void *)fixture_dll_a;case 0x200:return (void *)fixture_tls_b;case 0x201:return (void *)fixture_dll_b;
 case 0x500:return (void *)loc_1011C7AD;case 0x501:return (void *)loc_1011C79B;case 0x300:return (void *)fixture_start;case 0x400:return (void *)fixture_fls;case 0xfffff000:return (void *)tribes_return_to_host;default:abort();}}
static _Atomic unsigned notifications,inside,completed,finalized;
static _Thread_local unsigned sequence,slot;
static _Thread_local uint32_t fls_index;
static uint32_t identities[64],stacks[64],tebs[64];
static uint32_t word(_cpu *c,uint32_t p){uint32_t n;memcpy(&n,(void *)(uintptr_t)(c->_pointer_offset+p),4);return n;}
static void ret(_cpu *c,unsigned n,uint32_t result){assert(word(c,c->_esp)==0xfffff000);c->_esp+=4*(n+1);c->_eax=result;}
static void environment(_cpu *c){
 assert(x86_read_fs_dword(0x30)==0x10000);assert(word(c,x86_read_fs_dword(0x18)+0x30)==0x10000);
 assert(!tribes_call_guest(c,0x500,0,NULL,c->_esp-128));assert(!tribes_call_guest(c,0x501,0,NULL,c->_esp-128));
 assert(tribes_get_current_process_id()==123);assert(tribes_get_current_thread_id()==tribes_get_thread_id(0xfffffffe));
 assert(x86_read_fs_dword(0x18));assert(c->_pointer_offset+x86_read_fs_dword(4)==(uintptr_t)c->stack_top || (c->_esp>=0x30000000&&c->_esp<0x50000000));
 assert(c->_pointer_offset+x86_read_fs_dword(8)==(uintptr_t)c->stack_bottom || (c->_esp>=0x30000000&&c->_esp<0x50000000)); /* FLS owns a cleanup stack. */
 uint32_t tls=x86_read_fs_dword(0x2c),a=word(c,tls),b=word(c,tls+4);assert(x86_mov_reg_mem_32(tls)==a);
 assert(a&&b&&a!=b&&word(c,a)==0x12345678&&word(c,b)==0xabcdef01);
 assert(word(c,a+4)==0&&word(c,b+4)==0);
}
static void fixture_finalize(_cpu *c){environment(c);assert(sequence==5||sequence==9);atomic_fetch_add(&finalized,1);}
void fixture_notify(_cpu *c,uint32_t which){
 assert(atomic_fetch_add(&inside,1)==0);environment(c);
 uint32_t reason=word(c,c->_esp+8),image=word(c,c->_esp+4);assert(word(c,c->_esp+12)==0);
 assert(image==(which<2?0x100000:0x200000));
 if(reason==2){assert(sequence==which);assert(tribes_get_last_error()==(which?0x6000+which-1:0));tribes_set_last_error(0x6000+which);}
 else {assert(reason==3);static const unsigned order[]={2,3,0,1};assert(sequence>=5&&sequence<=8&&which==order[sequence-5]);assert(tribes_fls_get_value(c->_pointer_offset,fls_index)==0);}
 sequence++;if(sequence==9){assert(tribes_guest_finalizer_register(fixture_finalize));assert(tribes_fls_free_index(c->_pointer_offset,fls_index,NULL,NULL));atomic_fetch_add(&completed,1);}atomic_fetch_add(&notifications,1);assert(atomic_fetch_sub(&inside,1)==1);ret(c,3,1);
}
void fixture_cleanup(_cpu *c){environment(c);assert(sequence==4);assert(word(c,c->_esp+4)==slot+1);sequence++;ret(c,1,0);}
void fixture_procedure(_cpu *c){
 environment(c);assert(sequence==4);assert(tribes_guest_finalizer_register(fixture_finalize));assert(tribes_guest_finalizer_register(fixture_finalize));slot=word(c,c->_esp+4);assert(slot<64);
 if(slot==63){*(volatile unsigned char *)(uintptr_t)(c->_pointer_offset+x86_read_fs_dword(8)-1)=1;abort();}
 uint32_t tls_block=word(c,x86_read_fs_dword(0x2c));assert(word(c,tls_block+8)==0);uint32_t marker=slot+1;memcpy((void *)(uintptr_t)(c->_pointer_offset+tls_block+8),&marker,4);
 assert(x86_read_fs_dword(4)-x86_read_fs_dword(8)==(slot==0?0x800000:slot==1?0x20000:0x100000));
 identities[slot]=tribes_get_current_thread_id();stacks[slot]=x86_read_fs_dword(8);tebs[slot]=x86_read_fs_dword(0x18);
 uint32_t fields[7];assert(tribes_virtual_memory_query(c->_pointer_offset,stacks[slot]-4096,fields)&&fields[4]==0x2000);
 assert(tribes_get_last_error()==0x6003);tribes_set_last_error(0x7000+slot);
 uint32_t pseudo_args[]={0xfffffffe,c->_esp-64};
 assert(tribes_call_guest(c,3,2,pseudo_args,c->_esp-128));assert(word(c,c->_esp-64)==259);
 fls_index=tribes_fls_alloc(c,0x400);assert(fls_index!=UINT32_MAX&&tribes_fls_set(c,fls_index,slot+1));
 /* Make overlapping runnable lifetimes observable without relying on ordering. */
 struct timespec delay={0,1000000};nanosleep(&delay,NULL);assert(tribes_get_last_error()==0x7000+slot);assert(word(c,tls_block+8)==slot+1);assert(tribes_guest_callback_context()==c);
 if(slot%3==1){uint32_t code=0x9000+slot;(void)tribes_call_guest(c,5,1,&code,c->_esp-128);abort();}
 if(slot%3==2){uint32_t args[]={0x200000,0x9000+slot};(void)tribes_call_guest(c,6,2,args,c->_esp-128);abort();}
 ret(c,1,0x9000+slot);
}
/* Called after all notifications on this thread, via a real FLS dispose hook. */
unsigned fixture_notifications(void){return atomic_load(&notifications);}
void fixture_check(unsigned n){
 for(unsigned i=0;i<n;i++){assert(identities[i]&&stacks[i]&&tebs[i]);for(unsigned j=0;j<i;j++)assert(identities[i]!=identities[j]);}
 assert(atomic_load(&completed)==n&&atomic_load(&finalized)==2*n);
}

static void *host_entry(void *arg){return arg;}
void fixture_bad_host(unsigned mode,uint64_t base){
 pthread_t thread;pthread_attr_t attr;assert(!pthread_attr_init(&attr));
 if(mode==1)(void)pthread_create(&thread,&attr,(void *(*)(void *))(uintptr_t)0x12340,NULL);
 if(mode==2)(void)pthread_create(&thread,&attr,host_entry,(void *)(uintptr_t)(base+0x1000));
 if(mode==3)(void)pthread_create(&thread,(void *)(uintptr_t)(base+0x1000),host_entry,NULL);
 if(mode==4)(void)_setjmp(*(jmp_buf *)(uintptr_t)(base+0x1000));
 abort();
}
