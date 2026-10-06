#include "../port/core/guest_threads.h"
#include "../port/core/guest_modules.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_teb.h"
#include "../port/core/native_handles.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
static unsigned char *memory;static uint64_t base;
static _Atomic unsigned counts[4][4],inside,started,release_worker;
extern void loader_dll(void),loader_worker(void),tribes_return_to_host(void);
extern void tbimp_LoadLibraryA(void),tbimp_LoadLibraryExA(void),tbimp_LoadLibraryExW(void),tbimp_FreeLibrary(void),tbimp_DisableThreadLibraryCalls(void);
void *tribes_lookup(uint32_t pc){switch(pc){case 1:return tbimp_LoadLibraryA;case 2:return tbimp_LoadLibraryExA;case 3:return tbimp_LoadLibraryExW;case 4:return tbimp_FreeLibrary;case 5:return tbimp_DisableThreadLibraryCalls;case 0x100:return loader_dll;case 0x200:return loader_worker;case 0xfffff000:return tribes_return_to_host;default:abort();}}
static uint32_t word(_cpu *c,uint32_t at){uint32_t v;memcpy(&v,(void *)(uintptr_t)(c->_pointer_offset+at),4);return v;}
static void ret(_cpu *c,unsigned n,uint32_t result){assert(word(c,c->_esp)==0xfffff000);c->_esp+=4*(n+1);c->_eax=result;}
void loader_fixture_dll(_cpu *c){
 assert(atomic_fetch_add(&inside,1)==0);
 uint32_t image=word(c,c->_esp+4),reason=word(c,c->_esp+8);unsigned i=(image-0x1000000)/0x10000;
 assert(i<4&&reason<4&&!word(c,c->_esp+12));atomic_fetch_add(&counts[i][reason],1);
 assert(tribes_get_module_handle_ex_a(base,6,image,0x1000));
 if(reason==1&&i==2){uint32_t arg=image;assert(tribes_call_guest(c,5,1,&arg,c->_esp-128)==1);}
 struct timespec d={0,100000};nanosleep(&d,NULL);
 c->_ebx=0xdeadbeef;c->_esi=0x12345678;c->_eflags=0x297;
 atomic_fetch_sub(&inside,1);ret(c,3,i!=1||reason!=1);
}
void loader_fixture_worker(_cpu *c){
 if(word(c,c->_esp+4)){atomic_store(&started,1);while(!atomic_load(&release_worker)){struct timespec d={0,1000000};nanosleep(&d,NULL);}}
 ret(c,1,77);
}
static uint32_t call_at(uint32_t stack,uint32_t pc,unsigned n,const uint32_t *args){
 _cpu c={0};c._pointer_offset=base;c._esp=stack;c.stack_bottom=memory+stack-0x8000;c.stack_top=memory+stack;c._eflags=0x202;c._ebx=0x98765432;
 _cpu before=c;uint32_t result=tribes_call_guest(&c,pc,n,args,c._esp);assert(!memcmp(&c,&before,sizeof c));return result;
}
static uint32_t call(uint32_t pc,unsigned n,const uint32_t *a){return call_at(0x80000,pc,n,a);}
static void *load_race(void *arg){uint32_t stack=0x90000+0x10000*(uint32_t)(uintptr_t)arg,at=0x2600;for(unsigned j=0;j<10;j++)assert(call_at(stack,1,1,&at)==0x1030000);return NULL;}
int main(int argc,char **argv){
 struct rlimit core={0,0};assert(!setrlimit(RLIMIT_CORE,&core));
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory,0x100000,PROT_READ|PROT_WRITE));base=(uintptr_t)memory;
 const char *paths[]={"C:\\a.asi","C:\\fail.asi","C:\\disabled.asi","C:\\race.asi"};TribesThreadModule modules[4]={0};
 for(unsigned i=0;i<4;i++){uint32_t image=0x1000000+0x10000*i;assert(tribes_module_register_image(base,image,0x10000,paths[i],0));assert(tribes_module_set_pending(base,image,1));modules[i]=(TribesThreadModule){image,0x100,UINT32_MAX,0,0,0,NULL,NULL,1};strcpy((char *)memory+0x2000+i*0x200,paths[i]);}
 assert(tribes_threads_configure(base,123,42,0x100000,modules,4));assert(tribes_teb_configure(base,0x18000,0x80000,0x70000,123,42,0x19000));tribes_threads_process_ready(base);
 uint32_t name=0x2000,image=0x1000000;
 strcpy((char *)memory+0x2000,"C:\\.\\a.asi");
 assert(!tribes_get_module_handle(base,name));
 uint32_t worker=tribes_create_thread(base,0,0,0x200,1,0,0);assert(worker);while(!atomic_load(&started)){struct timespec d={0,1000000};nanosleep(&d,NULL);}
 assert(call(1,1,&name)==image&&counts[0][1]==1&&!counts[0][2]);assert(tribes_get_module_handle(base,name)==image);
 if(argc>1&&!strcmp(argv[1],"unload")){call(4,1,&image);abort();}
 uint32_t ex[]={0x2000,0,0};assert(call(2,3,ex)==image);assert(call(4,1,&image));
 const char *wide="C:/././A.ASI";for(unsigned i=0;i<=strlen(wide);i++){uint16_t v=(unsigned char)wide[i];memcpy(memory+0x3000+2*i,&v,2);}ex[0]=0x3000;assert(call(3,3,ex)==image&&counts[0][1]==1);assert(call(4,1,&image));
 const char *aliases[]={"C:\\.\\.\\a.asi","C:/./a.asi","C:\\a.asi"};
 for(unsigned i=0;i<3;i++){strcpy((char *)memory+0x3200,aliases[i]);uint32_t alias=0x3200;assert(call(1,1,&alias)==image&&counts[0][1]==1);assert(call(4,1,&image));}
 strcpy((char *)memory+0x3200,"C:\\other\\.\\a.asi");assert(!tribes_get_module_handle(base,0x3200));
 strcpy((char *)memory+0x3200,"C:\\a.asi\\.");assert(!tribes_get_module_handle(base,0x3200));
 uint32_t fail=0x2200;assert(!call(1,1,&fail)&&tribes_get_last_error()==1114&&counts[1][1]==1&&counts[1][0]==1);assert(!tribes_get_module_handle(base,fail));
 if(argc>1&&!strcmp(argv[1],"reload")){call(1,1,&fail);abort();}
 uint32_t disabled=0x2400;assert(call(1,1,&disabled)==0x1020000&&counts[2][1]==1);
 atomic_store(&release_worker,1);assert(tribes_wait_single_object(worker,10000)==0&&tribes_close_handle(worker));assert(counts[0][2]==0&&counts[0][3]==1);assert(!counts[2][2]&&!counts[2][3]);
 worker=tribes_create_thread(base,0,0,0x200,0,0,0);assert(worker&&tribes_wait_single_object(worker,10000)==0&&tribes_close_handle(worker));assert(counts[0][2]==1&&counts[0][3]==2);assert(!counts[2][2]&&!counts[2][3]);
 pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,load_race,(void *)(uintptr_t)i));for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));assert(counts[3][1]==1);
 uint32_t raced=0x1030000;for(unsigned i=0;i<39;i++)assert(call(4,1,&raced));assert(!tribes_guest_threads_live_count());
 puts("PASS: absolute dot/slash path aliases preserve identity/reference count and attach once, wrong-directory rejection; original-entry ABI, pending visibility, attach failure/detach, 40 concurrent loads with one attach, balanced added references, existing/new worker notifications and DisableThreadLibraryCalls");return 0;
}
