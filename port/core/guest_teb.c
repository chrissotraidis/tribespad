/* Sparse current-thread x86 FS contract for precompiled guest instructions.
   A nonzero self pointer is also used as allocator thread identity. Metadata
   is native-thread-local; arbitrary TEB/PEB access and SEH delivery are gates. */
#include "guest_teb.h"
#include "guest_heap.h"
#include "guest_system.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
typedef struct {uint64_t base;uint32_t address;int configured;} Context;
static _Thread_local Context current;
typedef struct Process {uint64_t base;uint32_t peb;struct Process *next;} Process;
static Process *processes;
static pthread_mutex_t process_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t process_peb(uint64_t base){
    pthread_mutex_lock(&process_lock);uint32_t result=0;
    for(Process *p=processes;p;p=p->next)if(p->base==base){result=p->peb;break;}
    pthread_mutex_unlock(&process_lock);return result;
}
static int range(uint64_t base,uint32_t address,uint32_t bytes){return address>=TRIBES_GUEST_MINIMUM_ADDRESS&&bytes&&(uint64_t)address+bytes<=(uint64_t)TRIBES_GUEST_MAXIMUM_ADDRESS+1&&base<=UINTPTR_MAX-address-(bytes-1);}
int tribes_teb_configure_process(uint64_t base,const TribesProcessMetadata *m){
    if(!base||!m||!range(base,m->peb,4096)||!range(base,m->parameters,4096)||m->peb%4096||m->parameters%4096||m->peb==m->parameters||
       !range(base,m->image,1)||m->image_path_bytes>65532||m->command_line_bytes>65532||(m->image_path_bytes|m->command_line_bytes)&1||
       !range(base,m->image_path,m->image_path_bytes+2)||!range(base,m->command_line,m->command_line_bytes+2)||!range(base,m->environment,4)){errno=EINVAL;return 0;}
    Process *p=calloc(1,sizeof *p);if(!p)return 0;p->base=base;p->peb=m->peb;
    pthread_mutex_lock(&process_lock);
    for(Process *old=processes;old;old=old->next)if(old->base==base){pthread_mutex_unlock(&process_lock);free(p);errno=EALREADY;return 0;}
    /* Ordinary emulated user process: no NT secure-process context or Windows
       Application Verifier. Normalized pointers refer to this guest window.
       Unknown loader/heap/NT fields remain outside this bounded contract. */
    unsigned char peb[0x70]={0};uint32_t parameters[0x4c/4]={0};
    memcpy(peb+8,&m->image,4);memcpy(peb+0x10,&m->parameters,4);
    parameters[0]=parameters[1]=sizeof parameters;parameters[2]=1;
    memcpy(parameters+6,m->handles,12);
    parameters[0x38/4]=m->image_path_bytes|((m->image_path_bytes+2)<<16);parameters[0x3c/4]=m->image_path;
    parameters[0x40/4]=m->command_line_bytes|((m->command_line_bytes+2)<<16);parameters[0x44/4]=m->command_line;
    parameters[0x48/4]=m->environment;
    memcpy((void *)(uintptr_t)(base+m->peb),peb,sizeof peb);
    memcpy((void *)(uintptr_t)(base+m->parameters),parameters,sizeof parameters);
    if(current.configured&&current.base==base)memcpy((void *)(uintptr_t)(base+current.address+0x30),&m->peb,4);
    p->next=processes;processes=p;pthread_mutex_unlock(&process_lock);return 1;
}
static _Noreturn void stop(const char *reason,uint32_t offset) {fprintf(stderr,"TribesPad: sparse TEB %s at FS:0x%x\n",reason,offset);abort();}
uint32_t tribes_teb_configure(uint64_t base,uint32_t teb,uint32_t stack_base,uint32_t stack_limit,uint32_t pid,uint32_t tid,uint32_t tls_array) {
    if(teb<TRIBES_GUEST_MINIMUM_ADDRESS||teb%4096||(uint64_t)teb+4096>(uint64_t)TRIBES_GUEST_MAXIMUM_ADDRESS+1||base>UINTPTR_MAX-teb-4095||!stack_limit||stack_base<=stack_limit||stack_base>TRIBES_GUEST_MAXIMUM_ADDRESS+1u||!pid||!tid||tls_array<TRIBES_GUEST_MINIMUM_ADDRESS||tls_array>TRIBES_GUEST_MAXIMUM_ADDRESS-3||tls_array%4||(teb<stack_base&&(uint64_t)teb+4096>stack_limit)) {tribes_set_last_error(87);return 0;}
    static const uint32_t offsets[]={0,4,8,0x18,0x20,0x24,0x2c,0x30,0xbf4};
    uint32_t values[]={UINT32_MAX,stack_base,stack_limit,teb,pid,tid,tls_array,process_peb(base),0};
    for(unsigned i=0;i<9;i++)memcpy((void *)(uintptr_t)(base+teb+offsets[i]),values+i,4);
    current=(Context){base,teb,1};return 1;
}
void tribes_teb_clear(void) {memset(&current,0,sizeof current);}
static uintptr_t field(uint32_t offset,int write) {
    if(!current.configured)stop("unconfigured thread",offset);
    uint32_t self;memcpy(&self,(void *)(uintptr_t)(current.base+current.address+0x18),4);
    if(self!=current.address)stop("self identity modified",offset);
    if(write?offset!=0:!(offset==0||offset==4||offset==8||offset==0x18||offset==0x20||offset==0x24||offset==0x2c||offset==0x30||offset==0xbf4))stop("undeclared field",offset);
    return (uintptr_t)(current.base+current.address+offset);
}
uint32_t x86_read_fs_dword(uint32_t offset) {uint32_t value;memcpy(&value,(void *)field(offset,0),4);return value;}
void x86_write_fs_dword(uint32_t offset,uint32_t value) {memcpy((void *)field(offset,1),&value,4);}
void tribes_teb_set_last_status(uint32_t status) {memcpy((void *)field(0xbf4,0),&status,4);}

int tribes_teb_get_ids(uint64_t base,uint32_t *pid,uint32_t *tid){if(!current.configured||current.base!=base)return 0;*pid=x86_read_fs_dword(0x20);*tid=x86_read_fs_dword(0x24);return 1;}
