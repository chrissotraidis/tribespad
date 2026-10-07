/* The pinned renderer uses one SRW object as a cross-thread completion fence:
   begin on the render thread, signal from the original job completion callback,
   wait/read on the render thread. These four finite AOT call sites use this
   explicit fence; ordinary SRW ownership semantics remain unchanged. */
#include "../llasm-support/llasm_cpu.h"
#include "guest_memory_checks.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static uint64_t active_base;
static pthread_t owner;
static unsigned waiting;
static unsigned phase; /* 0 idle, 1 working, 2 completed, 3 reading */
static const uint32_t lock_address=0x1046e3dc,job_address=0x1046e410;
static _Noreturn void stop(void){fputs("TribesPad: invalid original render-job handoff\n",stderr);abort();}
static void require(int ok){if(!ok)stop();}
static uint32_t word(uint64_t base,uint32_t address){
    const void *p=(const void *)(uintptr_t)(base+address);uint32_t value;
    uint32_t token=tribes_memory_access_begin(p,4,1);memcpy(&value,p,4);tribes_memory_access_end(token);return value;
}
static void publish(uint64_t base,uint32_t value){
    void *p=(void *)(uintptr_t)(base+lock_address);uint32_t token=tribes_memory_access_begin(p,4,3);
    uint32_t expected=phase==1?1:phase==3?2:0,old;memcpy(&old,p,4);require(old==expected);
    memcpy(p,&value,4);tribes_memory_access_end(token);
}
/* Called in place of the four original cdecl lock wrappers. Each original
   caller already pushed its return address and lock argument. Pop only that
   return; the translated caller still performs its original argument cleanup. */
void tribes_render_job_step(_cpu *cpu,uint32_t step){
    static const uint32_t returns[]={0x10043a7b,0x100439ba,0x10043afd,0x10043b8b};
    uint64_t base=cpu->_pointer_offset;uint32_t sp=cpu->_esp;
    require(step<4&&base&&base<=UINTPTR_MAX-UINT32_MAX&&sp<=UINT32_MAX-20);
    require(tribes_memory_code_revision(base)==66);
    require(word(base,sp)==returns[step]&&word(base,sp+4)==lock_address);
    if(step==1){
        require(word(base,sp+8)==0x10076526&&word(base,sp+12)==job_address);
        require(word(base,job_address+0x40)==0x10043960&&word(base,job_address+0x44)==0x100439b0);
    }
    require(!pthread_mutex_lock(&mutex));
    if(step==0){
        require(phase==0);publish(base,1);active_base=base;owner=pthread_self();phase=1;
    }else{
        require(active_base==base);
        if(step==1){
            require(phase==1);publish(base,0);phase=2;require(!pthread_cond_signal(&changed));
        }else{
            require(pthread_equal(owner,pthread_self()));
            if(step==2){
                require(phase==1||phase==2);
                while(phase==1){++waiting;require(!pthread_cond_wait(&changed,&mutex));--waiting;}
                require(phase==2);publish(base,2);phase=3;
            }else{
                require(phase==3);publish(base,0);phase=0;active_base=0;
            }
        }
    }
    require(!pthread_mutex_unlock(&mutex));cpu->_esp=sp+4;
}

uint32_t tribes_render_job_waiters(void){require(!pthread_mutex_lock(&mutex));uint32_t n=waiting;require(!pthread_mutex_unlock(&mutex));return n;}
