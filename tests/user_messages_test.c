#include "../port/core/guest_user_messages.h"
#include "../port/core/guest_teb.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_window_catalog.h"
#include "../port/core/performance_clock.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern void tribes_enter(_cpu *,uint32_t),tribes_return_to_host(void);
extern void tbimp_SetTimer(void),tbimp_KillTimer(void),tbimp_PeekMessageA(void),tbimp_GetMessageA(void),tbimp_DispatchMessageA(void),tbimp_PostMessageA(void),tbimp_PostThreadMessageA(void),tbimp_PostQuitMessage(void),tbimp_TestTimerProc(void);
void *tribes_lookup(uint32_t a){switch(a){case 1:return tbimp_SetTimer;case 2:return tbimp_KillTimer;case 3:return tbimp_PeekMessageA;case 4:return tbimp_GetMessageA;case 5:return tbimp_DispatchMessageA;case 6:return tbimp_PostMessageA;case 7:return tbimp_PostThreadMessageA;case 8:return tbimp_PostQuitMessage;case 100:return tbimp_TestTimerProc;case 0xfffff000:return tribes_return_to_host;default:abort();}}
static unsigned char *memory;static unsigned callbacks;static int cancel_in_callback;static _Atomic int phase;
void test_timer_proc(_cpu *cpu,uint32_t hwnd,uint32_t message,uint32_t id,uint32_t stamp){assert(!hwnd);assert(message==0x113&&id);assert(tribes_get_current_thread_id()==73);assert((uint32_t)(tribes_tick_count()-stamp)<1000);callbacks++;if(cancel_in_callback)assert(tribes_kill_timer(cpu->_pointer_offset,0,id));}
static uint32_t invoke(_cpu *cpu,uint32_t target,unsigned count,const uint32_t *args){_cpu before=*cpu;cpu->_esp-=4*count;memcpy(memory+cpu->_esp,args,4*count);tribes_enter(cpu,target);uint32_t result=cpu->_eax;if(target!=8)before._eax=result;assert(!memcmp(cpu,&before,sizeof before));return result;}
static void *worker(void *unused){
 (void)unused;uint64_t base=(uintptr_t)memory;assert(tribes_teb_configure(base,0x110000,0x200000,0x180000,51,74,0x111000));
 assert(tribes_set_timer(base,0,0,10,100));atomic_store(&phase,1);while(atomic_load(&phase)!=2)tribes_sleep(1);
 tribes_sleep(15);assert(tribes_post_thread_message(base,73,0x8002,123,456));_cpu cpu={0};cpu._pointer_offset=base;tribes_guest_thread_finalize(&cpu);tribes_teb_clear();return NULL;
}
int main(int argc,char **argv){
 memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);assert(!mprotect(memory+0x100000,0x100000,PROT_READ|PROT_WRITE));uint64_t base=(uintptr_t)memory;
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));assert(tribes_teb_configure(base,0x100000,0x200000,0x180000,51,73,0x101000));
 _cpu cpu={0};cpu._pointer_offset=base;cpu._esp=0x1f0000;cpu.stack_bottom=memory+0x180000;cpu.stack_top=memory+0x200000;cpu._eax=123;cpu._eflags=0x246;
 uint32_t timer[]={0,0,500,100};uint32_t id=invoke(&cpu,1,4,timer);assert(id);
 uint32_t peek[]={0x102000,0,0,0,0};assert(!invoke(&cpu,3,5,peek));timer[1]=id;timer[2]=30;assert(invoke(&cpu,1,4,timer)==id);
 uint32_t get[]={0x102000,0,0,0};uint32_t start=tribes_tick_count();assert(invoke(&cpu,4,4,get)==1);assert((uint32_t)(tribes_tick_count()-start)>=20);uint32_t *m=(void *)(memory+0x102000);assert(m[0]==0&&m[1]==0x113&&m[2]==id&&m[3]==100);
 uint32_t dispatch=0x102000;
 if(argc>1&&!strcmp(argv[1],"unreadable"))assert(tribes_memory_permissions_set(0x102000,4096,0));
 cancel_in_callback=1;assert(!invoke(&cpu,5,1,&dispatch));assert(callbacks==1);assert(!invoke(&cpu,3,5,peek));
 timer[1]=0;timer[2]=10;id=invoke(&cpu,1,4,timer);assert(id);tribes_sleep(35);memset(memory+0x10201c,0xac,4);
 if(argc>1&&!strcmp(argv[1],"readonly"))assert(tribes_memory_permissions_set(0x102000,4096,1));
 assert(invoke(&cpu,3,5,peek));uint32_t saved[7];memcpy(saved,m,28);tribes_sleep(20);assert(invoke(&cpu,3,5,peek));assert(!memcmp(saved,m,28)&&memory[0x10201c]==0xac);
 uint32_t kill[]={0,id};assert(invoke(&cpu,2,2,kill));peek[4]=1;assert(invoke(&cpu,3,5,peek));assert(!memcmp(saved,m,28));assert(!invoke(&cpu,3,5,peek));cancel_in_callback=0;assert(!invoke(&cpu,5,1,&dispatch));assert(callbacks==2);
 /* Posted work takes precedence over newly generated timer messages. */
 timer[2]=10;id=invoke(&cpu,1,4,timer);tribes_sleep(15);uint32_t post[]={0,0x8001,17,19};assert(invoke(&cpu,6,4,post));assert(invoke(&cpu,3,5,peek)&&m[1]==0x8001&&m[2]==17);assert(invoke(&cpu,3,5,peek)&&m[1]==0x113);kill[1]=id;assert(invoke(&cpu,2,2,kill));
 /* Window destruction cancels that window's future deadlines. */
 assert(tribes_window_add(base,0x73100001,0x400000,0));timer[0]=0x73100001;timer[1]=42;timer[2]=10;assert(invoke(&cpu,1,4,timer));tribes_window_remove(base,0x73100001);tribes_sleep(20);assert(!invoke(&cpu,3,5,peek));
 pthread_t thread;assert(!pthread_create(&thread,NULL,worker,NULL));while(!atomic_load(&phase))tribes_sleep(1);tribes_sleep(20);peek[2]=peek[3]=0x113;assert(!invoke(&cpu,3,5,peek));atomic_store(&phase,2);assert(invoke(&cpu,4,4,get)==1);assert(m[1]==0x8002&&m[2]==123&&m[3]==456);assert(!pthread_join(thread,NULL));
 uint32_t missing[]={74,0x8001,0,0};assert(!invoke(&cpu,7,4,missing)&&tribes_get_last_error()==1444);
 uint32_t quit=77;invoke(&cpu,8,1,&quit);get[2]=get[3]=0x8000;assert(!invoke(&cpu,4,4,get));assert(m[1]==0x12&&m[2]==77);
 tribes_guest_thread_finalize(&cpu);peek[2]=peek[3]=0;assert(!invoke(&cpu,3,5,peek));tribes_guest_thread_finalize(&cpu);tribes_teb_clear();
 puts("PASS: real timer deadlines, owner-thread callbacks/ABI, reset/coalescing, killed pending messages, post priority, blocking cross-thread wake, queue teardown and WM_QUIT");return 0;
}
