#include "../port/core/guest_console.h"
#include "../port/core/guest_console_callback.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
extern void tbimp_SetConsoleCtrlHandler(void),tbimp_FixtureA(void),tbimp_FixtureB(void),tbimp_FixtureC(void),tbimp_FixtureD(void),tribes_return_to_host(void),tribes_enter(_cpu *,uint32_t);
void *tribes_lookup(uint32_t pc){switch(pc){case 1:return (void *)tbimp_SetConsoleCtrlHandler;case 2:return (void *)tbimp_FixtureA;case 3:return (void *)tbimp_FixtureB;case 4:return (void *)tbimp_FixtureC;case 5:return (void *)tbimp_FixtureD;case 0xfffff000:return (void *)tribes_return_to_host;}abort();}
_Noreturn void tribes_unimplemented_dinput(_cpu *cpu,uint32_t method){(void)cpu;(void)method;abort();}
static unsigned char *memory,*other;
static pthread_t main_thread;
static atomic_uint ready,concurrent_calls;
static _Thread_local unsigned log_ids[32],log_events[32],logged,reenter;
static void setup(_cpu *cpu,unsigned id,unsigned char *window){memset(cpu,0xa5,sizeof *cpu);cpu->_pointer_offset=(uintptr_t)window;cpu->_esp=0x10f003+id*65536;cpu->stack_bottom=window+0x100000+id*65536;cpu->stack_top=window+0x100000+(id+1)*65536;}
static void setter(_cpu *cpu,uint32_t handler,uint32_t add,uint32_t expected,uint32_t error){
    unsigned char *m=(void *)(uintptr_t)cpu->_pointer_offset;uint32_t top=cpu->_esp,sp=top-8,args[]={handler,add};memcpy(m+sp,args,8);memset(m+sp-68,0x79,64);memset(m+top,0x79,64);cpu->_esp=sp;_cpu before=*cpu;
    tribes_set_last_error(123);tribes_enter(cpu,1);before._eax=expected;before._esp=top;assert(!memcmp(cpu,&before,sizeof before)&&!memcmp(m+sp,args,8)&&tribes_get_last_error()==error);for(unsigned i=0;i<64;i++)assert(m[sp-68+i]==0x79&&m[top+i]==0x79);
}
static uint32_t fixture(_cpu *cpu,uint32_t event,unsigned id){
    assert(logged<32);log_ids[logged]=id;log_events[logged++]=event;
    if(reenter&&id==4){setter(cpu,4,0,1,123);setter(cpu,5,1,1,123);}
    cpu->_ecx=0xdeadbeef;cpu->_edx=0x12345678;cpu->_eflags=0x246;cpu->_st_cw=0xb7f;tribes_set_last_error(77);atomic_fetch_add(&concurrent_calls,1);return id==3;
}
uint32_t fixture_a(_cpu *cpu,uint32_t event){return fixture(cpu,event,2);}
uint32_t fixture_b(_cpu *cpu,uint32_t event){return fixture(cpu,event,3);}
uint32_t fixture_c(_cpu *cpu,uint32_t event){return fixture(cpu,event,4);}
uint32_t fixture_d(_cpu *cpu,uint32_t event){return fixture(cpu,event,5);}
static void deliver(_cpu *cpu,uint32_t event,uint32_t expected,const unsigned *order,unsigned count){
    unsigned char *m=(void *)(uintptr_t)cpu->_pointer_offset;logged=0;tribes_set_last_error(123);_cpu before=*cpu;memset(m+cpu->_esp-256,0x79,64);memset(m+cpu->_esp,0x79,64);
    assert(tribes_console_dispatch(cpu,event)==expected);assert(logged==count);for(unsigned i=0;i<count;i++)assert(log_ids[i]==order[i]&&log_events[i]==event);
    if(count)before._st_cw=0xb7f;assert(!memcmp(cpu,&before,sizeof before)&&tribes_get_last_error()==(count?77:123));for(unsigned i=0;i<64;i++)assert(m[cpu->_esp-256+i]==0x79&&m[cpu->_esp+i]==0x79);
}
static void *worker(void *context){unsigned id=(unsigned)(uintptr_t)context;_cpu cpu;setup(&cpu,id+1,memory);atomic_fetch_add(&ready,1);while(atomic_load(&ready)<8){}
    for(unsigned i=0;i<1000;i++){
        setter(&cpu,2,1,1,123);logged=0;_cpu before=cpu;assert(tribes_console_dispatch(&cpu,0)==TRIBES_CONSOLE_HANDLED);assert(logged&&logged<=9&&log_ids[logged-1]==3);for(unsigned j=0;j+1<logged;j++)assert(log_ids[j]==2&&log_events[j]==0);before._st_cw=0xb7f;assert(!memcmp(&cpu,&before,sizeof cpu)&&tribes_get_last_error()==77);setter(&cpu,2,0,1,123);
    }return NULL;
}
typedef struct SignalJob {int signal_number;uint32_t event,result;} SignalJob;
static void *signal_worker(void *context){SignalJob *job=context;assert(!pthread_equal(pthread_self(),main_thread));sigset_t set;assert(!sigemptyset(&set)&&!sigaddset(&set,job->signal_number));int received=0;assert(!sigwait(&set,&received)&&received==job->signal_number);_cpu cpu;setup(&cpu,9,memory);logged=0;tribes_set_last_error(123);_cpu before=cpu;job->result=tribes_console_deliver_posix(&cpu,received);before._st_cw=0xb7f;assert(logged==1&&log_ids[0]==3&&log_events[0]==job->event&&!memcmp(&cpu,&before,sizeof cpu)&&tribes_get_last_error()==77);return NULL;}
static unsigned char *window(void){unsigned char *p=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(p!=MAP_FAILED&&!mprotect(p+0x100000,0xb0000,PROT_READ|PROT_WRITE));return p;}
int main(int argc,char **argv){main_thread=pthread_self();memory=window();other=window();_cpu cpu,isolated;setup(&cpu,0,memory);setup(&isolated,0,other);
    if(argc>1){assert(!strcmp(argv[1],"stack-bound"));setter(&cpu,3,1,1,123);cpu.stack_bottom=memory+cpu._esp;tribes_console_dispatch(&cpu,0);abort();}
    const unsigned cb[]={4,3},ca[]={4,2},da[]={5,2},b[]={3};
    setter(&cpu,2,0,0,87);deliver(&cpu,0,TRIBES_CONSOLE_TERMINATE,NULL,0);setter(&cpu,2,1,1,123);setter(&cpu,3,7,1,123);setter(&cpu,4,1,1,123);deliver(&cpu,0,TRIBES_CONSOLE_HANDLED,cb,2);
    setter(&cpu,3,0,1,123);deliver(&cpu,1,TRIBES_CONSOLE_TERMINATE,ca,2);setter(&cpu,3,1,1,123);setter(&cpu,3,1,1,123);deliver(&cpu,0,1,b,1);setter(&cpu,3,0,1,123);deliver(&cpu,0,1,b,1);setter(&cpu,3,0,1,123);setter(&cpu,3,0,0,87);
    setter(&cpu,0,UINT32_MAX,1,123);deliver(&cpu,0,TRIBES_CONSOLE_IGNORED,NULL,0);deliver(&cpu,1,4,ca,2);setter(&cpu,0,0,1,123);reenter=1;deliver(&cpu,0,4,ca,2);reenter=0;deliver(&cpu,0,4,da,2);
    deliver(&isolated,0,4,NULL,0);setter(&isolated,3,1,1,123);deliver(&isolated,0,1,b,1);deliver(&cpu,0,4,da,2);
    tribes_console_destroy((uintptr_t)memory);setter(&cpu,3,1,1,123);for(unsigned event=0;event<7;event++)if(event!=3&&event!=4)deliver(&cpu,event,event<2?1:5,b,1);
    logged=0;tribes_set_last_error(123);assert(!tribes_console_dispatch(&cpu,3)&&tribes_get_last_error()==87&&!logged);assert(!tribes_console_dispatch(NULL,0)&&tribes_get_last_error()==87);assert(!tribes_console_deliver_posix(&cpu,SIGTERM)&&tribes_get_last_error()==50);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(threads+i,NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));deliver(&cpu,0,1,b,1);setter(&cpu,2,0,0,87);
    sigset_t mask,old;assert(!sigemptyset(&mask)&&!sigaddset(&mask,SIGINT)&&!sigaddset(&mask,SIGQUIT)&&!sigaddset(&mask,SIGHUP)&&!pthread_sigmask(SIG_BLOCK,&mask,&old));
    SignalJob jobs[]={{SIGINT,0,0},{SIGQUIT,1,0},{SIGHUP,2,0}};for(unsigned i=0;i<3;i++){pthread_t thread;assert(!pthread_create(&thread,NULL,signal_worker,jobs+i)&&!pthread_kill(thread,jobs[i].signal_number)&&!pthread_join(thread,NULL));assert(jobs[i].result==(i<2?1:5));}assert(!pthread_sigmask(SIG_SETMASK,&old,NULL));
    TribesConsoleDispatch *snapshot=NULL;assert(tribes_console_begin((uintptr_t)memory,1,&snapshot));tribes_console_destroy((uintptr_t)memory);assert(tribes_console_next(snapshot)==3&&!tribes_console_next(snapshot));tribes_console_end(snapshot);deliver(&cpu,0,4,NULL,0);deliver(&isolated,0,1,b,1);tribes_console_destroy((uintptr_t)other);deliver(&isolated,0,4,NULL,0);
    printf("PASS: actual ARM64 registration wrapper and one-argument stdcall callbacks; LIFO, duplicate/remove/error/NULL-ignore, process isolation, reentrant mutation and immutable event snapshots, default/handled termination outcomes, full CPU/FP/flags/stack preservation and LastError; 8000 concurrent add/deliver/remove iterations and %u callbacks; real SIGINT/SIGQUIT/SIGHUP delivered via sigwait on dedicated guest threads, native signal mask restored, teardown and live-snapshot lifetime\n",atomic_load(&concurrent_calls));assert(!munmap(memory,UINT64_C(0x100000000))&&!munmap(other,UINT64_C(0x100000000)));return 0;
}
